#include "clang/AST/ASTContext.h"
#include "clang/AST/Type.h"
#include "clang/AST/RecordLayout.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Basic/SourceManager.h" // Needed for SourceManager
#include "clang/Basic/FileManager.h"  // Needed for FileEntry
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h" // For path manipulation
#include <fstream>
#include <sstream>
#include <memory>
#include <vector>
#include <map>
#include <set>
#include <string> // Added for std::string
#include <iostream>

using namespace clang;
using namespace clang::ast_matchers;
using namespace clang::tooling;
using namespace llvm;

static cl::OptionCategory MyToolCategory("MCPC Options");

// --- Command Line Options ---
static cl::opt<std::string> SigOutputFilename(
    "s",
    cl::desc("Specify function signature (JSON generation C code) output filename"),
    cl::value_desc("filename"),
    cl::Required,
    cl::cat(MyToolCategory));

static cl::opt<std::string> BridgeOutputFilename(
    "b",
    cl::desc("Specify main bridge (C code dispatcher) output filename"),
    cl::value_desc("filename"),
    cl::Required,
    cl::cat(MyToolCategory));

// NEW: Option for output directory for generated per-file bridges
static cl::opt<std::string> BridgeOutputDir(
    "o",
    cl::desc("Specify output directory for generated per-file bridge C/H files"),
    cl::value_desc("directory"),
    cl::init("."), // Default to current directory
    cl::cat(MyToolCategory));


// --- Persistent Data Structures (AST Independent) ---
// NEW: Store information without relying on live AST nodes

struct PersistentJsonSchemaInfo {
    std::string type; // "integer", "string", "boolean", "number", "object", "array"
    std::string ref;  // "#/$defs/..."
    std::unique_ptr<PersistentJsonSchemaInfo> items; // For arrays
    bool isEnum = false;
    std::string enumExportName; // Store enum name if isEnum is true

    // 默认构造函数
    PersistentJsonSchemaInfo() = default;

    // 拷贝构造函数
    PersistentJsonSchemaInfo(const PersistentJsonSchemaInfo& other) 
        : type(other.type),
          ref(other.ref),
          isEnum(other.isEnum),
          enumExportName(other.enumExportName) {
        if (other.items) {
            items = std::make_unique<PersistentJsonSchemaInfo>(*other.items);
        }
    }

    // 移动构造函数
    PersistentJsonSchemaInfo(PersistentJsonSchemaInfo&& other) noexcept
        : type(std::move(other.type)),
          ref(std::move(other.ref)),
          items(std::move(other.items)),
          isEnum(other.isEnum),
          enumExportName(std::move(other.enumExportName)) {}

    // 拷贝赋值运算符
    PersistentJsonSchemaInfo& operator=(const PersistentJsonSchemaInfo& other) {
        if (this != &other) {
            type = other.type;
            ref = other.ref;
            isEnum = other.isEnum;
            enumExportName = other.enumExportName;
            if (other.items) {
                items = std::make_unique<PersistentJsonSchemaInfo>(*other.items);
            } else {
                items.reset();
            }
        }
        return *this;
    }

    // 移动赋值运算符
    PersistentJsonSchemaInfo& operator=(PersistentJsonSchemaInfo&& other) noexcept {
        if (this != &other) {
            type = std::move(other.type);
            ref = std::move(other.ref);
            items = std::move(other.items);
            isEnum = other.isEnum;
            enumExportName = std::move(other.enumExportName);
        }
        return *this;
    }
};

struct PersistentFieldInfo {
    std::string name;
    std::string description;
    PersistentJsonSchemaInfo schemaInfo; // Store schema info directly
    std::string typeName; // Store type as string
};

struct PersistentParameterInfo {
    std::string name;
    std::string description;
    std::string typeName; // Store type as string
    PersistentJsonSchemaInfo schemaInfo; // Store schema info directly
    // bool isRequired; // TODO
};

struct PersistentEnumConstantInfo {
    std::string name;
    // std::string value; // If needed
};

struct PersistentEnumDefinition {
    std::string exportName;
    std::string originalName;
    std::string description;
    std::vector<PersistentEnumConstantInfo> constants;
    std::string underlyingTypeName; // Store type as string
    std::string sourceFileBase; // Base name of the source file (e.g., "my_enums")
    std::set<std::string> requiredIncludes; // Headers needed by this enum's parser
    PersistentJsonSchemaInfo schemaInfo; // Schema for the enum itself
};

struct PersistentStructDefinition {
    std::string exportName;
    std::string originalName;
    std::string description;
    std::vector<PersistentFieldInfo> fields;
    std::string sourceFileBase; // Base name of the source file (e.g., "my_structs")
    std::set<std::string> requiredIncludes; // Headers needed by this struct's parser
};

struct PersistentFunctionDefinition {
    std::string exportName;
    std::string originalName;
    std::string description;
    std::string returnTypeName; // Store type as string
    std::vector<PersistentParameterInfo> parameters;
    std::string sourceFileBase; // Base name of the source file (e.g., "my_functions")
    std::set<std::string> requiredIncludes; // Headers needed by this function's handler/includes
};

// --- Global Persistent Storage ---
// NEW: Maps keyed by export name for efficient lookup during final generation
std::map<std::string, PersistentEnumDefinition> g_persistentEnums;
std::map<std::string, PersistentStructDefinition> g_persistentStructs;
std::map<std::string, PersistentFunctionDefinition> g_persistentFunctions;
// Keep track of which source file bases need bridge files generated
std::set<std::string> g_processedFileBases;
// Store all unique includes needed for the final signature file
std::set<std::string> g_allRequiredIncludesForSig;

// --- Annotation Parser (Task 1.2 - Unchanged conceptually) ---
std::string getAnnotationValue(const clang::Decl* D, const std::string& annotationPrefix) {
    if (!D || !D->hasAttrs()) {
        return "";
    }
    for (const auto *Attr : D->getAttrs()) {
        if (const auto *Annotate = dyn_cast<AnnotateAttr>(Attr)) {
            llvm::StringRef annotation = Annotate->getAnnotation();
            if (annotation.starts_with(annotationPrefix)) {
                llvm::StringRef value = annotation.drop_front(annotationPrefix.length());
                 if (value.starts_with("\"") && value.ends_with("\"")) {
                    value = value.drop_front(1).drop_back(1);
                 } else if (value.starts_with("#")) {
                     value = value.drop_front(1);
                 }
                return value.str();
            }
        }
    }
    return "";
}

std::string getExportName(const clang::NamedDecl* D) {
    std::string exportName= "";
    if(D->hasAttr<AnnotateAttr>()) {
        exportName= getAnnotationValue(D, "EXPORT_AS=");
        if(exportName.empty()) {
            exportName = D->getNameAsString();
        }
    }

    return exportName;
}


// --- Type Analysis & JSON Schema Generation Helper ---
// MODIFIED: Returns PersistentJsonSchemaInfo, takes ASTContext temporarily
// Needs to be called *during* matching while ASTContext is valid.
PersistentJsonSchemaInfo getPersistentJsonSchemaInfoForType(QualType qualType, ASTContext& context) {
    PersistentJsonSchemaInfo schema;
    if (qualType.isNull()) {
        errs() << "Warning: Null type encountered, defaulting to object\n";
        schema.type = "object";
        return schema;
    }

    // Use canonical type for consistent checking
    QualType canonicalQualType = qualType.getCanonicalType();
    const clang::Type* type = canonicalQualType.getTypePtr();

    if (type->isBuiltinType()) {
        const clang::BuiltinType* builtin = cast<clang::BuiltinType>(type);
        switch (builtin->getKind()) {
            case BuiltinType::Bool: schema.type = "boolean"; break;
            case BuiltinType::Char_S:
            case BuiltinType::Char_U:
                 // Check if it's char* by looking at the original type, not canonical
                 if (qualType->isPointerType() && qualType->getPointeeType()->isCharType()) {
                      schema.type = "string";
                 } else {
                      schema.type = "string"; // Treat single char as string
                      errs() << "Warning: Treating single char type '" << qualType.getAsString() << "' as JSON string.\n";
                 }
                 break;
            case BuiltinType::UChar:
            case BuiltinType::SChar:
            case BuiltinType::Short:
            case BuiltinType::UShort:
            case BuiltinType::Int:
            case BuiltinType::UInt:
            case BuiltinType::Long:
            case BuiltinType::ULong:
            case BuiltinType::LongLong:
            case BuiltinType::ULongLong:
                schema.type = "integer"; break;
            case BuiltinType::Float:
            case BuiltinType::Double:
            case BuiltinType::LongDouble:
                schema.type = "number"; break;
            case BuiltinType::Void: // Handle void return type
                schema.type = "null"; // Or represent absence of return value? Use null for schema.
                break;
            default:
                schema.type = "string"; // Fallback
                errs() << "Warning: Unknown BuiltinType kind " << builtin->getKind() << ", defaulting to string for '" << qualType.getAsString() << "'\n";
                break;
        }
    } else if (type->isPointerType()) {
        const clang::PointerType* ptrType = cast<clang::PointerType>(type);
        clang::QualType pointeeType = ptrType->getPointeeType();
        const clang::Type* pointeeTypePtr = pointeeType.getCanonicalType().getTypePtr();

        if (pointeeType->isCharType()) {
            schema.type = "string";
        } else if (const clang::RecordType* recordType = pointeeTypePtr->getAs<clang::RecordType>()) {
             const clang::RecordDecl* recordDecl = recordType->getDecl();
             if (recordDecl->isStruct() || recordDecl->isUnion() || recordDecl->isClass()) { // Allow struct/union/class pointers
                 std::string exportName = getExportName(recordDecl);
                 if (!exportName.empty()) { // Check if the pointed-to struct is exported
                     // Check if it's already known persistently
                     if (g_persistentStructs.count(exportName)) {
                          schema.ref = "#/$defs/" + exportName;
                     } else {
                         // It might be defined in a later TU. Assume it *will* be defined.
                         schema.ref = "#/$defs/" + exportName;
                         errs() << "Info: Struct pointer '" << qualType.getAsString() << "' points to exported struct '" << exportName << "' defined elsewhere or later. Assuming $ref.\n";
                     }
                 } else {
                      schema.type = "object"; // Fallback if not exported
                      errs() << "Warning: Struct pointer '" << qualType.getAsString() << "' points to non-exported struct '" << recordDecl->getNameAsString() << "'. Defaulting to object.\n";
                 }
             } else {
                 schema.type = "object"; // Fallback for other record types?
                 errs() << "Warning: Pointer '" << qualType.getAsString() << "' points to unsupported record type. Defaulting to object.\n";
             }
        } else if (pointeeType->isVoidType()) {
            schema.type = "object"; // Represent void* as generic object/any? Object is safer.
             errs() << "Warning: Pointer type 'void*' not directly mapped to JSON schema. Defaulting to object.\n";
        } else {
            schema.type = "object"; // Generic fallback for other pointers (int*, etc.) - JSON doesn't have pointers
             errs() << "Warning: Pointer type '" << qualType.getAsString() << "' not directly mapped to JSON schema. Defaulting to object.\n";
        }
    } else if (const EnumType* enumType = type->getAs<EnumType>()) {
        const EnumDecl* enumDecl = enumType->getDecl();
        std::string exportName = getExportName(enumDecl);
        // Default to underlying type for JSON schema type
        schema = getPersistentJsonSchemaInfoForType(enumDecl->getIntegerType(), context);
        if (!exportName.empty()) {
            // Check persistent store
            if (g_persistentEnums.count(exportName)) {
                schema.ref = "#/$defs/" + exportName;
                schema.isEnum = true;
                schema.enumExportName = exportName;
            } else {
                 // Assume defined elsewhere/later
                 schema.ref = "#/$defs/" + exportName;
                 schema.isEnum = true;
                 schema.enumExportName = exportName;
                 errs() << "Info: Enum type '" << qualType.getAsString() << "' refers to exported enum '" << exportName << "' defined elsewhere or later. Assuming $ref.\n";
            }
        } else {
             errs() << "Warning: Enum type '" << qualType.getAsString() << "' has no EXPORT_AS annotation or name mismatch. Defaulting to its underlying type (" << schema.type << ").\n";
             // Keep the underlying type schema, clear ref/enum flags
             schema.ref = "";
             schema.isEnum = false;
             schema.enumExportName = "";
        }
    } else if (const RecordType* recordType = type->getAs<RecordType>()) {
        // Struct/Union/Class passed by value
         const RecordDecl* recordDecl = recordType->getDecl();
         if (recordDecl->isStruct() || recordDecl->isUnion() || recordDecl->isClass()) {
             std::string exportName = getExportName(recordDecl);
             if (!exportName.empty()) {
                 if (g_persistentStructs.count(exportName)) {
                    schema.ref = "#/$defs/" + exportName;
                 } else {
                    // Assume defined elsewhere/later
                    schema.ref = "#/$defs/" + exportName;
                    errs() << "Info: Struct type '" << qualType.getAsString() << "' refers to exported struct '" << exportName << "' defined elsewhere or later. Assuming $ref.\n";
                 }
             } else {
                  schema.type = "object"; // Fallback if not exported
                  errs() << "Warning: Struct '" << qualType.getAsString() << "' passed by value is not exported/known. Defaulting to object.\n";
             }
         } else {
              schema.type = "object"; // Fallback
             errs() << "Warning: Unsupported record type '" << qualType.getAsString() << "' passed by value. Defaulting to object.\n";
         }
    } else if (type->isArrayType()) {
        schema.type = "array";
        // For C arrays (e.g., int arr[10]), get element type
        // For pointers used as arrays (int* arr), this branch isn't hit. Pointer logic handles it.
        const clang::ArrayType* arrayType = context.getAsArrayType(canonicalQualType);
        if (arrayType) {
             clang::QualType elementType = arrayType->getElementType();
             schema.items = std::make_unique<PersistentJsonSchemaInfo>(getPersistentJsonSchemaInfoForType(elementType, context));
        } else {
            errs() << "Warning: Could not determine element type for array type '" << qualType.getAsString() << "'. Defaulting items to object.\n";
            schema.items = std::make_unique<PersistentJsonSchemaInfo>();
            schema.items->type = "object"; // Fallback item type
        }

    } else if (const clang::TypedefType* typedefType = type->getAs<clang::TypedefType>()) {
        // Look through the typedef
        return getPersistentJsonSchemaInfoForType(typedefType->desugar(), context);
    } else if (const clang::ElaboratedType* elaboratedType = type->getAs<clang::ElaboratedType>()) {
        // Look through elaborated type (e.g., struct MyStruct)
        return getPersistentJsonSchemaInfoForType(elaboratedType->getNamedType(), context);
    }
    else {
        schema.type = "object"; // General fallback
        errs() << "Warning: Unsupported type '" << qualType.getAsString() << "' encountered. Defaulting to object.\n";
    }

    return schema;
}


// --- Helper Functions ---

// NEW: Get base type name as string (safer than relying on QualType later)
std::string getBaseTypeNameStr(QualType qt) {
    QualType canonical = qt.getCanonicalType();
    const clang::Type* typePtr = canonical.getTypePtr();

    if (const RecordType *rt = typePtr->getAs<RecordType>()) {
        // Handle anonymous structs/unions with typedefs
        const RecordDecl *rd = rt->getDecl();
        if (!rd->getIdentifier() && rd->getTypedefNameForAnonDecl()) {
            return rd->getTypedefNameForAnonDecl()->getNameAsString();
        }
        return rd->getNameAsString(); // Might be empty for anonymous without typedef
    } else if (const EnumType *et = typePtr->getAs<EnumType>()) {
        return et->getDecl()->getNameAsString();
    } else if (const TypedefType *tt = typePtr->getAs<TypedefType>()) {
        // Return the typedef name itself, not the desugared type
        return tt->getDecl()->getNameAsString();
    }
    // Fallback for basic types or complex types not handled above
    // Use Clang's printer, but remove elaborated type keywords if present
    clang::LangOptions langOpts;
    clang::PrintingPolicy pp(langOpts);
    pp.SuppressTagKeyword = true; // Remove 'struct', 'enum'
    std::string typeStr = qt.getAsString(pp);
    return typeStr;

    // Original simpler fallback: return qt.getAsString();
}

// NEW: Get the base filename without extension or path
std::string getSourceFileBaseName(const Decl* D, ASTContext& Context) {
    SourceManager &SM = Context.getSourceManager();
    SourceLocation Loc = D->getLocation();
    if (Loc.isInvalid() || !Loc.isFileID()) return "";

    FileID FID = SM.getFileID(Loc);
    const FileEntry *FE = SM.getFileEntryForID(FID);
    if (!FE) return "";

    StringRef FullPath = FE->tryGetRealPathName();
    if (FullPath.empty()) {
        errs() << "Warning: Could not get real path for file FID '" << FID.getHashValue() << "'\n";
    }
    if (FullPath.empty()) return "";

    return sys::path::stem(FullPath).str(); // Gets filename without extension
}

// NEW: Helper to get includes for a Decl
std::set<std::string> getRequiredIncludesForDecl(const Decl* D, ASTContext& Context) {
    std::set<std::string> includes;
    SourceManager &SM = Context.getSourceManager();
    SourceLocation Loc = D->getLocation();
     if (Loc.isInvalid() || !Loc.isFileID()) return includes; // Cannot determine file

    // Heuristic: Add the header where the definition is located
    std::string headerPath = SM.getFilename(Loc).str();

     // Try to get a relative path or just the filename
     // This logic might need refinement based on project structure and include paths
     size_t last_slash = headerPath.find_last_of("/\\");
     if (last_slash != std::string::npos) {
         headerPath = headerPath.substr(last_slash + 1);
     }

     // Only add if it looks like a header file
     if (!headerPath.empty() &&
         (headerPath.find(".h") != std::string::npos || headerPath.find(".hpp") != std::string::npos))
     {
         includes.insert(headerPath);
     }
     // TODO: Add logic to find headers for dependent types (e.g., struct fields, function params)
     // This is complex and might require deeper AST analysis or relying on user includes.
     // For now, just include the definition's header.

    return includes;
}


// --- MatchFinder Callback Implementation ---
// MODIFIED: Populates persistent storage, not temporary globals
class ExportMatcher : public MatchFinder::MatchCallback {
     ASTContext *Context = nullptr; // Context valid only within run()

     // Helper to convert QualType to string safely
     std::string qualTypeToString(QualType qt) {
         if (qt.isNull()) return "void"; // Or some indicator of error/null
         return qt.getAsString();
     }

public:
     void run(const MatchFinder::MatchResult &Result) override {
         Context = Result.Context; // Capture context for this match

        std::string sourceFileBase; // Base name of the file where the decl is found

        // --- Enum Parsing ---
         if (const EnumDecl *ED = Result.Nodes.getNodeAs<EnumDecl>("enumDecl")) {
             if (ED->isThisDeclarationADefinition()) {
                 std::string exportName = getExportName(ED);
                 sourceFileBase = getSourceFileBaseName(ED, *Context);
                 if (!exportName.empty() && !sourceFileBase.empty()) {
                     if (g_persistentEnums.count(exportName)) {
                          // TODO: Handle duplicate export names? Merge? Error?
                          errs() << "Warning: Duplicate export name '" << exportName << "' for enum found in " << sourceFileBase << ". Ignoring duplicate definition.\n";
                          return;
                     }
                     errs() << "Processing Enum: " << ED->getNameAsString() << " (Exported As: " << exportName << ") in " << sourceFileBase << "\n";

                     PersistentEnumDefinition enumDef;
                     enumDef.exportName = exportName;
                     enumDef.originalName = getBaseTypeNameStr(Context->getTypeDeclType(ED)); // Use helper
                     enumDef.description = getAnnotationValue(ED, "DESCRIPTION=");
                     enumDef.underlyingTypeName = qualTypeToString(ED->getIntegerType());
                     enumDef.sourceFileBase = sourceFileBase;
                     enumDef.requiredIncludes = getRequiredIncludesForDecl(ED, *Context);
                     // Add the definition file itself to includes for signature file
                     g_allRequiredIncludesForSig.insert(enumDef.requiredIncludes.begin(), enumDef.requiredIncludes.end());


                     for (const EnumConstantDecl *ECD : ED->enumerators()) {
                         PersistentEnumConstantInfo constantInfo;
                         constantInfo.name = ECD->getNameAsString();
                         enumDef.constants.push_back(constantInfo);
                     }
                     // Get schema info *now*
                     enumDef.schemaInfo = getPersistentJsonSchemaInfoForType(Context->getTypeDeclType(ED), *Context);

                     g_persistentEnums[exportName] = std::move(enumDef);
                     g_processedFileBases.insert(sourceFileBase);
                 }
             }
         }
         // --- Struct Parsing ---
         else if (const RecordDecl *RD = Result.Nodes.getNodeAs<RecordDecl>("structDecl")) {
              // Process structs and unions (treat similarly for JSON)
              if ((RD->isStruct() || RD->isUnion()) && RD->isThisDeclarationADefinition()) {
                 std::string exportName = getExportName(RD);
                 sourceFileBase = getSourceFileBaseName(RD, *Context);
                 if (!exportName.empty() && !sourceFileBase.empty()) {
                     if (g_persistentStructs.count(exportName)) {
                         errs() << "Warning: Duplicate export name '" << exportName << "' for struct/union found in " << sourceFileBase << ". Ignoring duplicate definition.\n";
                         return;
                     }
                     errs() << "Processing Struct/Union: " << RD->getNameAsString() << " (Exported As: " << exportName << ") in " << sourceFileBase << "\n";

                     PersistentStructDefinition structDef;
                     structDef.exportName = exportName;
                     structDef.originalName = getBaseTypeNameStr(Context->getRecordType(RD)); // Use helper
                     structDef.description = getAnnotationValue(RD, "DESCRIPTION=");
                     structDef.sourceFileBase = sourceFileBase;
                     structDef.requiredIncludes = getRequiredIncludesForDecl(RD, *Context);
                     // Add the definition file itself to includes for signature file
                      g_allRequiredIncludesForSig.insert(structDef.requiredIncludes.begin(), structDef.requiredIncludes.end());


                     // Handle potentially anonymous struct/union names
                     if (structDef.originalName.empty()) {
                         if (RD->getIdentifier()) {
                             structDef.originalName = RD->getNameAsString(); // Shouldn't happen if helper works
                         } else {
                             // Try harder to find a typedef name
                             const TypedefNameDecl* TDD = RD->getTypedefNameForAnonDecl();
                             if (TDD) {
                                  structDef.originalName = TDD->getNameAsString();
                             } else {
                                  // Last resort placeholder
                                  structDef.originalName = exportName + (RD->isUnion() ? "_union" : "_struct");
                                 errs() << "Warning: Anonymous struct/union exported as " << exportName << ". Using placeholder C name '" << structDef.originalName << "'.\n";
                             }
                         }
                     }


                     for (const FieldDecl *FD : RD->fields()) {
                          PersistentFieldInfo fieldInfo;
                          fieldInfo.name = FD->getNameAsString();
                          if (fieldInfo.name.empty()) {
                              errs() << "Warning: Skipping unnamed field in struct/union '" << exportName << "'\n";
                              continue; // Skip unnamed fields (like in anonymous unions within structs)
                          }
                          fieldInfo.description = getAnnotationValue(FD, "DESCRIPTION=");
                          fieldInfo.typeName = qualTypeToString(FD->getType());
                          // Get schema info *now*
                          fieldInfo.schemaInfo = getPersistentJsonSchemaInfoForType(FD->getType(), *Context);
                          structDef.fields.push_back(std::move(fieldInfo));
                          // Add includes required by field types? Complex. Start simple.
                     }
                     g_persistentStructs[exportName] = std::move(structDef);
                     g_processedFileBases.insert(sourceFileBase);
                 }
              }
         }
        // --- Function Parsing ---
        else if (const FunctionDecl *FD = Result.Nodes.getNodeAs<FunctionDecl>("exportedFunction")) {
            // Only process definitions that have a body
             if (!FD->isThisDeclarationADefinition() || !FD->doesThisDeclarationHaveABody()) return;

            std::string exportName = getExportName(FD);
            sourceFileBase = getSourceFileBaseName(FD, *Context);

            if (!exportName.empty() && !sourceFileBase.empty()) {
                if (g_persistentFunctions.count(exportName)) {
                    errs() << "Warning: Duplicate export name '" << exportName << "' for function found in " << sourceFileBase << ". Ignoring duplicate definition.\n";
                    return;
                }
                errs() << "Processing Function: " << FD->getNameAsString() << " (Exported As: " << exportName << ") in " << sourceFileBase << "\n";

                PersistentFunctionDefinition funcDef;
                funcDef.exportName = exportName;
                funcDef.originalName = FD->getNameAsString();
                funcDef.description = getAnnotationValue(FD, "DESCRIPTION=");
                funcDef.returnTypeName = qualTypeToString(FD->getReturnType());
                errs() << "Function " << funcDef.originalName << " Return type: " << funcDef.returnTypeName << "\n";
                funcDef.sourceFileBase = sourceFileBase;
                funcDef.requiredIncludes = getRequiredIncludesForDecl(FD, *Context);
                // Add the definition file itself to includes for signature file
                 g_allRequiredIncludesForSig.insert(funcDef.requiredIncludes.begin(), funcDef.requiredIncludes.end());

                for (unsigned i = 0; i < FD->getNumParams(); ++i) {
                    const ParmVarDecl *PVD = FD->getParamDecl(i);
                    PersistentParameterInfo paramInfo;
                    paramInfo.name = PVD->getNameAsString();
                     if (paramInfo.name.empty()) {
                         paramInfo.name = "param" + std::to_string(i + 1);
                         errs() << "Warning: Unnamed parameter found in function " << funcDef.originalName << ", using generated name '" << paramInfo.name << "'\n";
                     }
                    paramInfo.description = getAnnotationValue(PVD, "DESCRIPTION=");
                    paramInfo.typeName = qualTypeToString(PVD->getType());
                     // Get schema info *now*
                    paramInfo.schemaInfo = getPersistentJsonSchemaInfoForType(PVD->getType(), *Context);
                    funcDef.parameters.push_back(std::move(paramInfo));
                    // Add includes required by param types? Complex.
                }
                 g_persistentFunctions[exportName] = std::move(funcDef);
                 g_processedFileBases.insert(sourceFileBase);
            }
        }
     } // end run()
};


// --- Per-File Bridge Code Generation Functions ---
// NEW: Functions to generate code into specific .c/.h files

// Helper to open output streams for per-file generation
bool openPerFileOutputStreams(const std::string& baseName, std::unique_ptr<raw_fd_ostream>& cOS, std::unique_ptr<raw_fd_ostream>& hOS) {
    std::error_code EC;
    SmallString<256> cPath(BridgeOutputDir);
    sys::path::append(cPath, baseName + "_bridge.c");

    SmallString<256> hPath(BridgeOutputDir);
    sys::path::append(hPath, baseName + "_bridge.h");

     // Ensure the output directory exists
    EC = llvm::sys::fs::create_directories(BridgeOutputDir);
    if (EC) {
        errs() << "Error: Could not create output directory '" << BridgeOutputDir << "': " << EC.message() << "\n";
        return false;
    }


    cOS = std::make_unique<raw_fd_ostream>(cPath, EC, sys::fs::OF_Text);
    if (EC) {
        errs() << "Error opening per-file C bridge file " << cPath << ": " << EC.message() << "\n";
        return false;
    }

    hOS = std::make_unique<raw_fd_ostream>(hPath, EC, sys::fs::OF_Text);
    if (EC) {
        errs() << "Error opening per-file H bridge file " << hPath << ": " << EC.message() << "\n";
        cOS.reset(); // Close the C file if H file failed
        return false;
    }
    return true;
}

void generatePerFileHeaderBoilerplate(raw_fd_ostream &hOS, const std::string& baseName, const std::set<std::string>& requiredIncludes) {
    std::string guard = "GENERATED_" + StringRef(baseName).upper() + "_BRIDGE_H";
    hOS << "// Generated bridge header for " << baseName << " (do not edit)\n";
    hOS << "#ifndef " << guard << "\n";
    hOS << "#define " << guard << "\n\n";
    hOS << "#include \"cJSON.h\"\n";
    hOS << "// Add any other common includes needed by handlers/parsers if necessary\n";
    hOS << "#include <stdbool.h> // For bool type if used\n\n";

    hOS << "// Include original headers required by definitions in " << baseName << "\n";
    for(const std::string& include : requiredIncludes) { // Use includes from first item found for this base
        hOS << "#include \"" << include << "\"\n";
    }
    hOS << "#ifdef __cplusplus\nextern \"C\" {\n#endif\n\n";
}

void generatePerFileFooter(raw_fd_ostream &cOS, raw_fd_ostream &hOS) {
    hOS << "\n#ifdef __cplusplus\n} // extern \"C\"\n#endif\n\n";
    hOS << "#endif // Header guard\n";
    hOS.flush();
    cOS.flush();
}

void generateEnumParser(raw_fd_ostream &cOS, raw_fd_ostream &hOS, const PersistentEnumDefinition& enumDef) {
    std::string funcName = "parse_" + enumDef.exportName;
    // Declaration in Header
    hOS << "// Parser for enum " << enumDef.exportName << " (" << enumDef.originalName << ")\n";
    hOS << enumDef.originalName << " " << funcName << "(cJSON *json);\n\n";

    // Definition in C file
    cOS << "// Parser for enum " << enumDef.exportName << " (" << enumDef.originalName << ")\n";
    cOS << "static inline " << enumDef.originalName << " " << funcName << "(cJSON *json) {\n"; // Make static inline if only used here
    cOS << "    if (!json) return (" << enumDef.originalName << ")0; // Default/error value\n";
    cOS << "    if (cJSON_IsString(json)) {\n";
    cOS << "        const char* str = json->valuestring;\n";
    for (size_t i = 0; i < enumDef.constants.size(); ++i) {
        const auto& constant = enumDef.constants[i];
        cOS << "        " << (i > 0 ? "else if" : "if") << " (strcmp(str, \"" << constant.name << "\") == 0) {\n";
        cOS << "            return " << constant.name << "; // Use original C constant name\n";
        cOS << "        }\n";
    }
    cOS << "        else {\n";
    cOS << "            fprintf(stderr, \"Error: Unknown string value '%s' for enum " << enumDef.exportName << "\\n\", str);\n";
    cOS << "            return (" << enumDef.originalName << ")0; // Default/error value\n";
    cOS << "        }\n";
    cOS << "    } else if (cJSON_IsNumber(json)) {\n";
    cOS << "        // Allow number input, assuming it corresponds to the enum value\n";
    cOS << "        return (" << enumDef.originalName << ")json->valueint;\n";
    cOS << "    } else {\n";
    cOS << "         fprintf(stderr, \"Error: Unexpected JSON type for enum " << enumDef.exportName << "\\n\");\n";
    cOS << "        return (" << enumDef.originalName << ")0; // Default/error value\n";
    cOS << "    }\n";
    cOS << "}\n\n";
}

// Forward declare struct parser generation for mutual recursion if needed
void generateStructParser(raw_fd_ostream &cOS, raw_fd_ostream &hOS, const PersistentStructDefinition& structDef);

void generateFieldParserLogic(raw_fd_ostream &cOS, const PersistentFieldInfo& field, const std::string& cJsonVar) {
     const auto& schema = field.schemaInfo;
     const std::string cVar = "obj->" + field.name;

     cOS << "        // Field: " << field.name << " (" << field.typeName << ")\n";
     cOS << "        cJSON* " << field.name << "_json = cJSON_GetObjectItem(" << cJsonVar << ", \"" << field.name << "\");\n";
     cOS << "        if (" << field.name << "_json && !cJSON_IsNull(" << field.name << "_json)) {\n"; // Check field exists and is not null

     if (!schema.ref.empty()) {
        // Reference to another struct or enum
        std::string referencedExportName;
        size_t defsPos = schema.ref.find("#/$defs/");
        if (defsPos != std::string::npos) {
            referencedExportName = schema.ref.substr(defsPos + strlen("#/$defs/"));
        }

        if (!referencedExportName.empty()) {
            bool isStructRef = g_persistentStructs.count(referencedExportName);
            bool isEnumRef = g_persistentEnums.count(referencedExportName);

             if (isStructRef) {
                 // Check if field is a pointer or value type
                 bool isPointer = StringRef(field.typeName).contains('*');
                 std::string parserFunc = "parse_" + referencedExportName;
                 // Ensure the referenced struct definition is available
                 if (g_persistentStructs.count(referencedExportName)) {
                    const auto& referencedStruct = g_persistentStructs.at(referencedExportName);
                    std::string structCType = referencedStruct.originalName; // Use original C type name
                     if (isPointer) {
                         cOS << "            " << cVar << " = " << parserFunc << "(" << field.name << "_json);\n";
                         cOS << "            // Note: Memory for " << field.name << " allocated by " << parserFunc << " needs careful management.\n";
                     } else {
                         // Struct by value: parse to temp pointer, copy, free temp
                         cOS << "            struct " << structCType << "* temp_" << field.name << " = " << parserFunc << "(" << field.name << "_json);\n";
                         cOS << "            if (temp_" << field.name << ") {\n";
                         cOS << "                " << cVar << " = *temp_" << field.name << "; // Copy value\n";
                         cOS << "                free(temp_" << field.name << "); // Free temporary allocated struct\n";
                         cOS << "            } else {\n";
                         cOS << "                fprintf(stderr, \"Warning: Failed to parse struct value for field '" << field.name << "'\\n\");\n";
                          cOS << "                // Initialize " << cVar << " safely (e.g., memset or default init)\n";
                          cOS << "                memset(&(" << cVar << "), 0, sizeof(" << cVar << "));\n";
                         cOS << "            }\n";
                     }
                 } else {
                      cOS << "            // Error: Referenced struct " << referencedExportName << " definition not found!\n";
                      if (isPointer) cOS << "            " << cVar << " = NULL;\n";
                      else cOS << "            memset(&(" << cVar << "), 0, sizeof(" << cVar << "));\n";

                 }

             } else if (isEnumRef) {
                 std::string parserFunc = "parse_" + referencedExportName;
                 cOS << "            " << cVar << " = " << parserFunc << "(" << field.name << "_json);\n";
             } else {
                 cOS << "            // Warning: Cannot determine type of $ref '" << schema.ref << "' for field '" << field.name << "'\n";
             }

        } else {
             cOS << "            // Warning: Invalid $ref format '" << schema.ref << "' for field '" << field.name << "'\n";
        }

     } else if (schema.type == "string") {
          // Check if C type is char* or char[]
         bool isPointer = StringRef(field.typeName).contains('*');
         bool isArray = StringRef(field.typeName).contains('[');
          cOS << "            if (cJSON_IsString(" << field.name << "_json)) {\n";
          if (isPointer) {
               cOS << "                " << cVar << " = _strdup(" << field.name << "_json->valuestring);\n";
               cOS << "                // TODO: Remember to free this string (" << field.name << ") later!\n";
          } else if (isArray) {
                cOS << "                strncpy(" << cVar << ", " << field.name << "_json->valuestring, sizeof(" << cVar << ") - 1);\n";
                cOS << "                " << cVar << "[sizeof(" << cVar << ") - 1] = '\\0'; // Ensure null termination\n";
          } else { // single char
               cOS << "                if (strlen(" << field.name << "_json->valuestring) > 0) {\n";
               cOS << "                   " << cVar << " = " << field.name << "_json->valuestring[0];\n";
               cOS << "                }\n";
          }
          cOS << "            } else {\n";
          cOS << "                 fprintf(stderr, \"Warning: Expected string for field '" << field.name << "' but got different type.\\n\");\n";
          cOS << "            }\n";
     } else if (schema.type == "integer") {
          cOS << "            if (cJSON_IsNumber(" << field.name << "_json)) {\n";
          cOS << "                " << cVar << " = (" << field.typeName << ")" << field.name << "_json->valueint; // Cast needed?\n";
          cOS << "            }\n";
     } else if (schema.type == "boolean") {
          cOS << "            if (cJSON_IsBool(" << field.name << "_json)) {\n";
          cOS << "                " << cVar << " = cJSON_IsTrue(" << field.name << "_json);\n";
          cOS << "            }\n";
     } else if (schema.type == "number") {
          cOS << "            if (cJSON_IsNumber(" << field.name << "_json)) {\n";
          cOS << "                " << cVar << " = (" << field.typeName << ")" << field.name << "_json->valuedouble; // Cast needed?\n";
          cOS << "            }\n";
     } else if (schema.type == "array") {
         cOS << "            // TODO: Implement array parsing for field '" << field.name << "'\n";
          // Needs to know element type, allocate array, loop through cJSON array, parse elements recursively
         cOS << "            fprintf(stderr, \"Warning: Array parsing for field '" << field.name << "' not implemented yet.\\n\");\n";
     } else if (schema.type == "object") {
          cOS << "            // Warning: Cannot parse generic 'object' type for field '" << field.name << "'. Needs specific type or $ref.\n";
     } else {
         cOS << "            // Warning: Unsupported schema type '" << schema.type << "' for field '" << field.name << "'\n";
     }

     cOS << "        } else {\n";
     cOS << "            // Field '" << field.name << "' missing or null in JSON. obj->" << field.name << " remains initialized (likely 0/NULL).\n";
     cOS << "            // Add specific default handling if needed.\n";
     cOS << "        }\n\n";
}


void generateStructParser(raw_fd_ostream &cOS, raw_fd_ostream &hOS, const PersistentStructDefinition& structDef) {
    std::string funcName = "parse_" + structDef.exportName;
    std::string structCType = structDef.originalName; // Use original C type name

    // Declaration in Header
    hOS << "// Parser for struct " << structDef.exportName << " (" << structCType << ")\n";
     // Need 'struct' keyword if original name doesn't imply it (like a typedef)
     // Heuristic: if originalName is the same as exportName + "_struct", it was likely anonymous
     bool needsStructKeyword = !structDef.originalName.empty() && !isupper(structDef.originalName[0]); // Simple check: typedefs often start upper
     if (structDef.originalName.find("_struct") != std::string::npos && structDef.originalName.rfind("_struct") == (structDef.originalName.length() - 7)){
          needsStructKeyword = true;
     }
     if (structDef.originalName.find("_union") != std::string::npos && structDef.originalName.rfind("_union") == (structDef.originalName.length() - 6)){
          needsStructKeyword = true; // Treat union similarly
     }

    hOS << (needsStructKeyword ? "struct " : "") << structCType << "* " << funcName << "(cJSON *json);\n\n";

    // Definition in C file
    cOS << "// Parser for struct " << structDef.exportName << " (" << structCType << ")\n";
    // Make static inline if only used within this file's handlers? Or keep extern? Let's keep extern for now.
    cOS << (needsStructKeyword ? "struct " : "") << structCType << "* " << funcName << "(cJSON *json) {\n";
    cOS << "    if (!json || !cJSON_IsObject(json)) return NULL;\n";
    cOS << "    " << (needsStructKeyword ? "struct " : "") << structCType << "* obj = (" << (needsStructKeyword ? "struct " : "") << structCType << "*)malloc(sizeof(" << (needsStructKeyword ? "struct " : "") << structCType << "));\n";
    cOS << "    if (!obj) { perror(\"malloc failed for " << structCType << "\"); return NULL; }\n";
    cOS << "    memset(obj, 0, sizeof(" << (needsStructKeyword ? "struct " : "") << structCType << ")); // Initialize memory\n\n";

    for (const auto& field : structDef.fields) {
        generateFieldParserLogic(cOS, field, "json");
    }

    cOS << "    return obj;\n";
    cOS << "}\n\n";
}

void generateFunctionHandler(raw_fd_ostream &cOS, raw_fd_ostream &hOS, const PersistentFunctionDefinition& funcDef) {
    std::string handlerFuncName = funcDef.originalName;
    std::string resultJsonVar = "result_json";

    // Declaration in Header
    hOS << "// Handler for function " << funcDef.exportName << " (calls " << funcDef.originalName << ")\n";
    hOS << "cJSON* handle_" << handlerFuncName << "(cJSON *params);\n\n";

    // Definition in C file
    cOS << "// Handler for function " << funcDef.exportName << " (calls " << funcDef.originalName << ")\n";
    cOS << "cJSON* handle_" << handlerFuncName << "(cJSON *params) {\n";
    cOS << "    cJSON* result_json = NULL;\n";
    cOS << "    if (!params || !cJSON_IsObject(params)) {\n";
    cOS << "        fprintf(stderr, \"Error: Invalid parameters object for function " << funcDef.exportName << "\\n\");\n";
    cOS << "        return NULL; // TODO: Return JSON error object?\n";
    cOS << "    }\n\n";

    cOS << "    // --- Declare and Parse Parameters --- \n";
    std::vector<std::string> allocated_params; // Track params needing free()
    for (const auto& param : funcDef.parameters) {
        cOS << "    " << param.typeName << " p_" << param.name << "; // Might need initialization\n";
        // Initialize pointers to NULL, others often to 0 via memset later or explicit init
        if (StringRef(param.typeName).contains('*')) {
             cOS << "    p_" << param.name << " = NULL;\n";
        } else {
            // For non-pointers, zero-init might be good practice depending on type
             cOS << "    memset(&p_" << param.name << ", 0, sizeof(p_" << param.name << "));\n";
        }
        // Generate parsing logic for this parameter
        PersistentFieldInfo tempFieldInfo; // Adapt field parsing logic for parameters
        tempFieldInfo.name = param.name;
        tempFieldInfo.typeName = param.typeName;
        tempFieldInfo.schemaInfo = param.schemaInfo;
        // Need to wrap param parsing logic similar to struct field parsing
        // Create a temporary cJSON object representing the parameter to reuse generateFieldParserLogic? No, parse directly.

        cOS << "    {\n"; // Scope for p_json
        cOS << "        cJSON* p_json = cJSON_GetObjectItem(params, \"" << param.name << "\");\n";
        cOS << "        if (p_json && !cJSON_IsNull(p_json)) {\n";

        const auto& schema = param.schemaInfo;
        const std::string cVar = "p_" + param.name; // Parameter variable name

        if (!schema.ref.empty()) {
             std::string referencedExportName;
             size_t defsPos = schema.ref.find("#/$defs/");
             if (defsPos != std::string::npos) {
                 referencedExportName = schema.ref.substr(defsPos + strlen("#/$defs/"));
             }
             if (!referencedExportName.empty()) {
                 bool isStructRef = g_persistentStructs.count(referencedExportName);
                 bool isEnumRef = g_persistentEnums.count(referencedExportName);
                 errs() << "schema.ref: " << schema.ref << "\n";
                 errs() << "isStructRef: " << isStructRef << "\n";
                 errs() << "isEnumRef: " << isEnumRef << "\n";
                 errs() << "param.typeName: " << param.typeName << "\n";
                  if (isStructRef && StringRef(param.typeName).contains('*')) { // Pointer to struct
                      cOS << "            " << cVar << " = parse_" << referencedExportName << "(p_json);//" << referencedExportName << "\n";
                      allocated_params.push_back(cVar); // Mark for freeing
                  } else if (isEnumRef) { // Enum (passed by value)
                       cOS << "            " << cVar << " = parse_" << referencedExportName << "(p_json);\n";
                  } else {
                     cOS << "            fprintf(stderr, \"Warning: Unsupported $ref type or pass-by-value struct for parameter '" << param.name << "'\\n\");\n";
                      // TODO: Handle struct-by-value parameters if needed (parse to temp, copy)
                  }
             } else {
                 cOS << "             fprintf(stderr, \"Warning: Invalid $ref format '" << schema.ref << "' for parameter '" << param.name << "'\\n\");\n";
             }
        } else if (schema.type == "string" && StringRef(param.typeName).contains('*')) { // Only handle char* for params easily
             cOS << "            if (cJSON_IsString(p_json)) {\n";
             cOS << "                " << cVar << " = _strdup(p_json->valuestring);\n";
             allocated_params.push_back(cVar); // Mark for freeing
             cOS << "            } else { fprintf(stderr, \"Warning: Expected string for param '" << param.name << "'\\n\"); }\n";
        } else if (schema.type == "integer") {
            cOS << "            if (cJSON_IsNumber(p_json)) { " << cVar << " = (" << param.typeName << ")p_json->valueint; }\n";
        } else if (schema.type == "boolean") {
             cOS << "            if (cJSON_IsBool(p_json)) { " << cVar << " = cJSON_IsTrue(p_json); }\n";
        } else if (schema.type == "number") {
             cOS << "            if (cJSON_IsNumber(p_json)) { " << cVar << " = (" << param.typeName << ")p_json->valuedouble; }\n";
        } else if (schema.type == "array") {
             cOS << "            // TODO: Implement array parsing for parameter '" << param.name << "'\n";
        } else {
             cOS << "            fprintf(stderr, \"Warning: Unsupported type for parameter '" << param.name << "'\\n\");\n";
        }

        cOS << "        } else {\n";
        // Parameter missing or null. Check if required? (Assume required for now)
        cOS << "            fprintf(stderr, \"Error: Required parameter '" << param.name << "' missing or null for function " << funcDef.exportName << "\\n\");\n";
        // Cleanup already allocated params before returning error
        
        cOS << "            goto END;\n";
        
        cOS << "        }\n";
        cOS << "    }\n"; // End scope for p_json
    }
    cOS << "\n";

    cOS << "    // --- Call Original C Function --- \n";
    bool hasReturn = funcDef.returnTypeName != "void";
    if (hasReturn) {
         cOS << "    " << funcDef.returnTypeName << " return_value;\n";
         cOS << "    return_value = " << funcDef.originalName << "(";

    } else {
        cOS << "    " << funcDef.originalName << "(";
    }
    for (size_t p_idx = 0; p_idx < funcDef.parameters.size(); ++p_idx) {
        cOS << (p_idx > 0 ? ", " : "") << "p_" << funcDef.parameters[p_idx].name;
    }
    cOS << ");\n\n";
    cOS << "    result_json = return_value;\n";
    cOS << "END:\n";
    cOS << "    // --- Free Allocated Parameter Memory --- \n";
    for(const auto& alloc_param : allocated_params) {
       cOS << "    if (" << alloc_param << ") free(" << alloc_param << ");\n";
    }
    cOS << "\n";


    cOS << "    // --- Convert Return Value to cJSON --- \n";
    errs() << "Function " << funcDef.originalName << " Return type: " << funcDef.returnTypeName << "\n";
    errs() << "hasReturn: " << hasReturn << "\n";
    if (hasReturn) {
        // TODO: Implement C -> cJSON conversion based on returnTypeName and its schema
        // This is the inverse of the parsing logic. Needs careful implementation
        // for basic types, structs, enums, strings.
         cOS << "    // TODO: Convert '" << funcDef.returnTypeName << "' return_value to cJSON*\n";
         // Example for int:
         if (funcDef.returnTypeName == "int" || funcDef.returnTypeName == "long" /* add other integer types */) {
              cOS << "    " << resultJsonVar << " = cJSON_CreateNumber((double)return_value);\n";
         }
         // Example for char*:
         else if (funcDef.returnTypeName == "char*" || funcDef.returnTypeName == "const char*") {
              cOS << "    if (return_value) {\n";
              cOS << "        " << resultJsonVar << " = cJSON_CreateString(return_value);\n";
              cOS << "        // Assuming caller of bridge manages freeing of returned string from original func?\n";
               cOS << "       // If original func returned malloc'd string, bridge might need to free it after creating JSON\n";
              cOS << "    } else {\n";
              cOS << "        " << resultJsonVar << " = cJSON_CreateNull();\n";
              cOS << "    }\n";
         }
         // Example for struct* (assuming a toJson_StructType function exists)
         else if (StringRef(funcDef.returnTypeName).contains('*')) { // Pointer type
             // Check if it points to an exported struct
             bool isStructPtr = false;
             std::string pointeeType = funcDef.returnTypeName;
             if (pointeeType.back() == '*') pointeeType.pop_back(); // Simple removal
             pointeeType = StringRef(pointeeType).trim().str();
             std::string structExportName;
             for(const auto& [expName, psd] : g_persistentStructs) {
                 if (psd.originalName == pointeeType || ("struct " + psd.originalName) == pointeeType) {
                     isStructPtr = true;
                     structExportName = expName;
                     break;
                 }
             }
             cOS << "    if(result_json == NULL) {\n";
             if (isStructPtr && !structExportName.empty()) {
                 cOS << "        // TODO: Need a function: cJSON* toJson_" << structExportName << "(" << funcDef.returnTypeName << " data);\n";
                 cOS << "        // " << resultJsonVar << " = toJson_" << structExportName << "(return_value);\n";
                 cOS << "        fprintf(stderr, \"Warning: C-to-JSON conversion for struct pointer return type '" << funcDef.returnTypeName << "' not implemented.\\n\");\n";
                 cOS << "        " << resultJsonVar << " = cJSON_CreateNull(); // Placeholder\n";

             } else {
                 cOS << "        fprintf(stderr, \"Warning: C-to-JSON conversion for return type '" << funcDef.returnTypeName << "' not implemented.\\n\");\n";
                 cOS << "        " << resultJsonVar << " = cJSON_CreateNull(); // Placeholder\n";
             }
             cOS << "    }\n";
         }
         else {
             cOS << "    fprintf(stderr, \"Warning: C-to-JSON conversion for return type '" << funcDef.returnTypeName << "' not implemented.\\n\");\n";
             cOS << "    " << resultJsonVar << " = cJSON_CreateNull(); // Placeholder\n";
         }

    } else {
         cOS << "    " << resultJsonVar << " = cJSON_CreateNull(); // Void function returns null\n";
    }
    cOS << "\n    return " << resultJsonVar << ";\n";
    cOS << "}\n\n";
}


// --- Clang AST Consumer ---
// MODIFIED: No longer holds generation logic directly. Calls matchers.
// Relies on FrontendAction to call final generation steps.
class ExportASTConsumer : public ASTConsumer {
     ExportMatcher MatcherCallback; // Callback instance
     MatchFinder Finder;
     ASTContext *Context = nullptr; // Keep context for the duration of this TU

public:
     // Constructor now takes persistent storage references if needed, but Matcher handles it
     ExportASTConsumer(ASTContext *Ctx) : Context(Ctx)
     {
         errs() << "ExportASTConsumer creating matchers...\n";
         // Define Matchers, associate them with MatcherCallback
         Finder.addMatcher(
             enumDecl(allOf(hasAttr(attr::Annotate), isDefinition())).bind("enumDecl"),
             &MatcherCallback
         );
         Finder.addMatcher(
             recordDecl(allOf(isStruct(), hasAttr(attr::Annotate), isDefinition())).bind("structDecl"),
             &MatcherCallback
         );
         Finder.addMatcher(
              recordDecl(allOf(isUnion(), hasAttr(attr::Annotate), isDefinition())).bind("structDecl"), // Match unions too
              &MatcherCallback
         );
         Finder.addMatcher(
             functionDecl(allOf(hasAttr(attr::Annotate), isDefinition(), hasBody(compoundStmt()))).bind("exportedFunction"),
             &MatcherCallback
         );
         errs() << "ExportASTConsumer matchers created.\n";
     }

     void HandleTranslationUnit(ASTContext &Ctx) override {
        clang::StringRef realpathname = Ctx.getSourceManager().getFileEntryForID(Ctx.getSourceManager().getMainFileID())->tryGetRealPathName();
         errs() << "ExportASTConsumer::HandleTranslationUnit for file: "
                << realpathname.str() << "\n";

         // Run the matchers *on this specific translation unit*
         // The MatcherCallback will populate the *global persistent* stores
         Finder.matchAST(Ctx);

         errs() << "ExportASTConsumer::HandleTranslationUnit - MatchFinder finished.\n";
         // NO generation happens here anymore.
         // NO state clearing happens here - state is persistent now.
     }
};

// --- Frontend Action ---
// MODIFIED: Does *not* take output streams. Creates the consumer.
// Added a destructor to trigger final generation.
class ExportAction : public ASTFrontendAction {
    // No streams needed here anymore

public:
    ExportAction() {
         errs() << "ExportAction created.\n";
    }

    // NEW: Destructor to trigger final generation after all TUs processed
    ~ExportAction() override {
        errs() << "ExportAction finished processing all files. Starting final generation...\n";
        FinalizeGeneration();
        errs() << "Final generation complete.\n";
    }


    std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI, StringRef InFile) override {
        errs() << "ExportAction creating ASTConsumer for: " << InFile << "\n";
        // The consumer just runs matchers which populate global persistent stores
        return std::make_unique<ExportASTConsumer>(&CI.getASTContext());
    }

private:
    // --- Final Generation Logic (Called after all TUs are processed) ---

    // Helper to generate JSON schema C code recursively (for signature file)
    void generateJsonSchemaCCode(raw_fd_ostream &os, const PersistentJsonSchemaInfo& schemaInfo, const std::string& parentVar, const std::string& keyNameOrIndex, bool isProperty) {
        std::string schemaVar = parentVar + "_" + keyNameOrIndex + "_schema";
        // Sanitize keyNameOrIndex if it's numeric (for array items)
        if (!keyNameOrIndex.empty() && std::isdigit(keyNameOrIndex[0])) {
            schemaVar = parentVar + "_items_schema"; // Simpler name for array items
        } else {
            // Sanitize key name for C variable
            std::string sanitizedKey = keyNameOrIndex;
            std::replace(sanitizedKey.begin(), sanitizedKey.end(), '-', '_');
             std::replace(sanitizedKey.begin(), sanitizedKey.end(), '.', '_');
            schemaVar = parentVar + "_" + sanitizedKey + "_schema";
        }


        os << "                    cJSON* " << schemaVar << " = cJSON_CreateObject();\n";
        os << "                    if (" << schemaVar << ") {\n";

        if (!schemaInfo.ref.empty()) {
            os << "                    cJSON_AddStringToObject(" << schemaVar << ", \"$ref\", \"" << schemaInfo.ref << "\");\n";
            os << "                    cJSON_AddStringToObject(" << schemaVar << ", \"type\", \"object\");\n";
        } else if (schemaInfo.type == "array" && schemaInfo.items) {
            os << "                    cJSON_AddStringToObject(" << schemaVar << ", \"type\", \"array\");\n";
            // Recursively generate items schema, key is "items"
            generateJsonSchemaCCode(os, *schemaInfo.items, schemaVar, "items", false);
        } else {
             os << "                    cJSON_AddStringToObject(" << schemaVar << ", \"type\", \"" << schemaInfo.type << "\");\n";
             // Handle direct enum values if needed (though we prefer $ref)
             if (schemaInfo.isEnum && schemaInfo.ref.empty() && g_persistentEnums.count(schemaInfo.enumExportName)) {
                  const auto& enumDef = g_persistentEnums.at(schemaInfo.enumExportName);
                  os << "                    cJSON* enum_values_inline = cJSON_CreateArray();\n";
                  os << "                    if(enum_values_inline){\n";
                   for (const auto& constant : enumDef.constants) {
                       os << "                    cJSON_AddItemToArray(enum_values_inline, cJSON_CreateString(\"" << constant.name << "\"));\n";
                   }
                  os << "                    cJSON_AddItemToObject(" << schemaVar << ", \"enum\", enum_values_inline);\n";
                  os << "                    }\n";
             }
        }
        // Add description if available (needs PersistentJsonSchemaInfo to hold it)
        // if (!schemaInfo.description.empty()) { os << "            cJSON_AddStringToObject(" << schemaVar << ", \"description\", \"" << schemaInfo.description << "\");\n"; }


        // Add the generated schema object to its parent
        if (isProperty) { // Adding to properties object
             os << "            cJSON_AddItemToObject(" << parentVar << ", \"" << keyNameOrIndex << "\", " << schemaVar << ");\n";
        } else if (schemaInfo.type == "array") { // Adding items schema to an array schema
             os << "            cJSON_AddItemToObject(" << parentVar << ", \"items\", " << schemaVar << ");\n";
        } else {
             // Should not happen if called correctly? Adding to defs?
              os << "           // Adding schema " << schemaVar << " to " << parentVar << " with key " << keyNameOrIndex << "\n";
               os << "            cJSON_AddItemToObject(" << parentVar << ", \"" << keyNameOrIndex << "\", " << schemaVar << ");\n";
        }
         os << "        } // end if (" << schemaVar << ")\n";
    }

    // Generates the `get_all_function_signatures_json` function into the signature file
    void generateSignaturesAndDefsFile(raw_fd_ostream &sigOS) {
        errs() << "Generating signatures and defs file: " << SigOutputFilename << "\n";
        sigOS << "// Function Signature JSON Generation Code (Auto-generated - Do not modify)\n";
        sigOS << "// Generated on: " << /* TODO: Add timestamp */ "\n";
        sigOS << "#include \"cJSON.h\"\n";
        sigOS << "#include <string.h> // For strcmp\n";
        sigOS << "#include <stdio.h> // for fprintf, stderr\n";
        sigOS << "#include <stdlib.h> // For malloc, free? (Maybe not needed here)\n\n";

        // Include necessary C headers (struct/enum definitions) gathered from all files
        sigOS << "// Original Header Includes:\n";
        for(const std::string& include : g_allRequiredIncludesForSig) {
             sigOS << "#include \"" << include << "\"\n";
        }
        sigOS << "\n";

        sigOS << "#ifdef __cplusplus\nextern \"C\" {\n#endif\n\n";

        sigOS << "cJSON* get_all_function_signatures_json() {\n";
        sigOS << "    cJSON* root = cJSON_CreateObject();\n";
        sigOS << "    if (!root) { fprintf(stderr, \"Failed to create root JSON object\\n\"); return NULL; }\n\n";

        // --- Generate $defs ---
        sigOS << "    // --- $defs --- \n";
        sigOS << "    cJSON* defs = cJSON_AddObjectToObject(root, \"$defs\");\n";
        sigOS << "    if (!defs) { fprintf(stderr, \"Failed to create $defs object\\n\"); cJSON_Delete(root); return NULL; }\n\n";

        // $defs for Enums
        for (const auto& [exportName, enumDef] : g_persistentEnums) {
            sigOS << "    // Definition for enum: " << exportName << "\n";
            sigOS << "    cJSON* enum_def = cJSON_CreateObject();\n";
            sigOS << "    {\n";
            sigOS << "        cJSON* enum_def = cJSON_CreateObject();\n";
            sigOS << "        if (enum_def) {\n";
            if (!enumDef.description.empty()) {
                sigOS << "            cJSON_AddStringToObject(enum_def, \"description\", \"" << escapeString(enumDef.description) << "\");\n";
            }
            // Use the stored schema info for the enum type itself
            sigOS << "            cJSON_AddStringToObject(enum_def, \"type\", \"" << enumDef.schemaInfo.type << "\");\n";
             // Add enum values (as strings)
            sigOS << "            cJSON* enum_values = cJSON_CreateArray();\n";
            sigOS << "            if (enum_values) {\n";
            for (const auto& constant : enumDef.constants) {
                sigOS << "                cJSON_AddItemToArray(enum_values, cJSON_CreateString(\"" << constant.name << "\"));\n";
            }
            sigOS << "                cJSON_AddItemToObject(enum_def, \"enum\", enum_values);\n";
            sigOS << "            }\n";
            sigOS << "            cJSON_AddItemToObject(defs, \"" << exportName << "\", enum_def);\n";
            sigOS << "        } // end if enum_def\n";
            sigOS << "    }\n\n";
        }
        sigOS << "    cJSON* field_schema_obj = NULL;\n";
        // $defs for Structs
        for (const auto& [exportName, structDef] : g_persistentStructs) {
            sigOS << "    // Definition for struct: " << exportName << "\n";
            sigOS << "    {\n";
            sigOS << "        cJSON* struct_def = cJSON_CreateObject();\n";
            sigOS << "        if (struct_def) {\n";
            if (!structDef.description.empty()) {
                sigOS << "            cJSON_AddStringToObject(struct_def, \"description\", \"" << escapeString(structDef.description) << "\");\n";
            }
            sigOS << "            cJSON_AddStringToObject(struct_def, \"type\", \"object\");\n";
            sigOS << "            cJSON* properties = cJSON_CreateObject();\n";
            sigOS << "            cJSON* required_props = cJSON_CreateArray();\n";
            sigOS << "            if (properties && required_props) {\n";

            for (const auto& field : structDef.fields) {
                sigOS << "                // Field: " << field.name << "\n";
                // Generate the schema C code for this field
                generateJsonSchemaCCode(sigOS, field.schemaInfo, "properties", field.name, true);
                // Add description at the field level within generateJsonSchemaCCode if needed

                 // Assume all struct fields are required unless they are pointers? Refine later.
                 if (!StringRef(field.typeName).contains('*')) {
                      sigOS << "                cJSON_AddItemToArray(required_props, cJSON_CreateString(\"" << field.name << "\"));\n";
                 }

                 // Add field description to the generated schema object if possible
                 // Find the generated object and add description
                 sigOS << "                field_schema_obj = cJSON_GetObjectItem(properties, \"" << field.name << "\");\n";
                 sigOS << "                if (field_schema_obj && !cJSON_HasObjectItem(field_schema_obj, \"description\") && !" << std::string(field.description.empty() ? "true" : "false") << ") {\n";
                 sigOS << "                     cJSON_AddStringToObject(field_schema_obj, \"description\", \"" << escapeString(field.description) << "\");\n";
                 sigOS << "                }\n";

            }
            sigOS << "                cJSON_AddItemToObject(struct_def, \"properties\", properties);\n";
             // Only add required array if it's not empty
            sigOS << "                if(cJSON_GetArraySize(required_props) > 0) {\n";
            sigOS << "                    cJSON_AddItemToObject(struct_def, \"required\", required_props);\n";
            sigOS << "                } else {\n";
            sigOS << "                    cJSON_Delete(required_props); // Clean up empty array\n";
            sigOS << "                }\n";
            sigOS << "            } else { /* Handle properties/required alloc failure */ \n";
             sigOS << "                if (properties) cJSON_Delete(properties);\n";
             sigOS << "                if (required_props) cJSON_Delete(required_props);\n";
             sigOS << "            }\n"; // end if properties && required
            sigOS << "            cJSON_AddItemToObject(defs, \"" << exportName << "\", struct_def);\n";
            sigOS << "        } // end if struct_def\n";
            sigOS << "    }\n\n";
        }


        // --- Generate tools ---
        sigOS << "    // --- tools --- \n";
        sigOS << "    cJSON* tools = cJSON_AddArrayToObject(root, \"tools\");\n";
        sigOS << "    if (!tools) { fprintf(stderr, \"Failed to create tools array\\n\"); cJSON_Delete(root); return NULL; }\n\n";

        for (const auto& [exportName, funcDef] : g_persistentFunctions) {
            sigOS << "    // Tool for function: " << funcDef.exportName << "\n";
            sigOS << "    {\n";
            sigOS << "        cJSON* tool = cJSON_CreateObject();\n";
            sigOS << "        if (tool) {\n";
            sigOS << "            cJSON_AddStringToObject(tool, \"name\", \"" << funcDef.exportName << "\");\n";
            sigOS << "            cJSON_AddStringToObject(tool, \"description\", \"" << escapeString(funcDef.description) << "\");\n";

            sigOS << "            cJSON* inputSchema = cJSON_CreateObject();\n";
            sigOS << "            if (inputSchema) {\n";
            sigOS << "                cJSON_AddStringToObject(inputSchema, \"type\", \"object\");\n";
            sigOS << "                cJSON_AddStringToObject(inputSchema, \"$schema\", \"http://json-schema.org/draft-07/schema#\");\n";
            sigOS << "                cJSON* properties = cJSON_CreateObject();\n";
            sigOS << "                cJSON* required = cJSON_CreateArray();\n";
            sigOS << "                if (properties && required) {\n";
            sigOS << "                cJSON* param_schema_obj = NULL;\n";
            for (const auto& param : funcDef.parameters) {
                 sigOS << "                    // Parameter: " << param.name << "\n";
                 // Generate schema C code for the parameter
                 generateJsonSchemaCCode(sigOS, param.schemaInfo, "properties", param.name, true);

                  // Assume required unless default value specified (TODO)
                 sigOS << "                    cJSON_AddItemToArray(required, cJSON_CreateString(\"" << param.name << "\"));\n";

                   // Add param description to the generated schema object
                   sigOS << "                    param_schema_obj = cJSON_GetObjectItem(properties, \"" << param.name << "\");\n";
                   sigOS << "                    if (param_schema_obj && !cJSON_HasObjectItem(param_schema_obj, \"description\") && !" << std::string(param.description.empty() ? "true" : "false") << ") {\n";
                   sigOS << "                         cJSON_AddStringToObject(param_schema_obj, \"description\", \"" << escapeString(param.description) << "\");\n";
                   sigOS << "                    }\n";
                 // TODO: Add default value if available
            }
            sigOS << "                    cJSON_AddItemToObject(inputSchema, \"properties\", properties);\n";
             // Only add required array if not empty
            sigOS << "                if (cJSON_GetArraySize(required) > 0) {\n";
            sigOS << "                    cJSON_AddItemToObject(inputSchema, \"required\", required);\n";
            sigOS << "                } else {\n";
            sigOS << "                    cJSON_Delete(required);\n";
            sigOS << "                }\n";
            sigOS << "                } else { /* Handle properties/required alloc failure */ \n";
             sigOS << "                   if (properties) cJSON_Delete(properties);\n";
             sigOS << "                   if (required) cJSON_Delete(required);\n";
             sigOS << "                   cJSON_Delete(inputSchema); inputSchema = NULL;\n";
             sigOS << "               }\n"; // end if properties && required

             sigOS << "                if (inputSchema) {\n"; // Check again before adding
             sigOS << "                    cJSON_AddFalseToObject(inputSchema, \"additionalProperties\");\n";
             sigOS << "                    cJSON_AddItemToObject(tool, \"inputSchema\", inputSchema);\n";
             sigOS << "                }\n";
            sigOS << "            } // end if inputSchema\n";

            sigOS << "            if (inputSchema) { // Only add tool if input schema was successful\n";
            sigOS << "               cJSON_AddItemToArray(tools, tool);\n";
            sigOS << "            } else { cJSON_Delete(tool); } // Clean up tool if schema failed\n";
            sigOS << "        } // end if tool\n";
            sigOS << "    }\n\n";
        }

        sigOS << "    return root;\n";
        sigOS << "}\n\n";

        sigOS << "#ifdef __cplusplus\n} // extern \"C\"\n#endif\n";
        sigOS.flush();
    }

    // Generates the main bridge dispatcher function into the bridge file
    void generateMainBridgeFile(raw_fd_ostream &bridgeOS) {
        errs() << "Generating main bridge file: " << BridgeOutputFilename << "\n";
        bridgeOS << "// Main Bridge Dispatcher Code (Auto-generated - Do not modify)\n";
         bridgeOS << "// Generated on: " << /* TODO: Add timestamp */ "\n";
        bridgeOS << "#include \"cJSON.h\"\n";
        bridgeOS << "#include <string.h> // For strcmp\n";
        bridgeOS << "#include <stdio.h>  // For fprintf, stderr\n";
        bridgeOS << "#include <stdlib.h> // For free (maybe needed by handlers?)\n\n";

        bridgeOS << "// Include generated bridge headers for each processed file base\n";
        for (const std::string& baseName : g_processedFileBases) {
            bridgeOS << "#include \"" << baseName << "_bridge.h\"\n";
        }
        bridgeOS << "\n";

        bridgeOS << "#ifdef __cplusplus\nextern \"C\" {\n#endif\n\n";

        bridgeOS << "// --- Main Bridge Function --- \n";
        bridgeOS << "cJSON* bridge(cJSON* input_json) {\n";
        bridgeOS << "    if (!input_json) {\n";
        bridgeOS << "        fprintf(stderr, \"Error: Bridge input JSON is NULL\\n\");\n";
        bridgeOS << "        return NULL;\n";
        bridgeOS << "    }\n\n";

        bridgeOS << "    cJSON* method_item = cJSON_GetObjectItemCaseSensitive(input_json, \"method\");\n";
        bridgeOS << "    cJSON* params_item = cJSON_GetObjectItemCaseSensitive(input_json, \"params\");\n\n";

        bridgeOS << "    if (!method_item || !cJSON_IsString(method_item) || !method_item->valuestring) {\n";
        bridgeOS << "        fprintf(stderr, \"Error: Invalid or missing 'method' string in input JSON\\n\");\n";
        bridgeOS << "        // TODO: Return error JSON\n";
        bridgeOS << "        return NULL;\n";
        bridgeOS << "    }\n";
         // Params are optional for some functions, but should be object if present
         bridgeOS << "    if (params_item && !cJSON_IsObject(params_item)) {\n";
         bridgeOS << "        fprintf(stderr, \"Error: 'params' field exists but is not a JSON object\\n\");\n";
         bridgeOS << "        // TODO: Return error JSON\n";
         bridgeOS << "        return NULL;\n";
         bridgeOS << "    }\n";
         bridgeOS << "    // Use empty object if params is missing or null, simplifies handlers\n";
         bridgeOS << "    cJSON* params_obj = (params_item && cJSON_IsObject(params_item)) ? params_item : cJSON_CreateObject(); \n";
         bridgeOS << "    bool params_allocated = (!params_item || !cJSON_IsObject(params_item)); // Track if we allocated it\n\n";


        bridgeOS << "    const char* func_name = method_item->valuestring;\n";
        bridgeOS << "    cJSON* result = NULL;\n\n";

        // --- Function Dispatch ---
        bridgeOS << "    bool handled = false;\n";
        for (const auto& [exportName, funcDef] : g_persistentFunctions) {
             bridgeOS << "    if (!handled && strcmp(func_name, \"" << funcDef.exportName << "\") == 0) {\n";
             bridgeOS << "        result = handle_" << funcDef.originalName << "(params_obj);\n";
             bridgeOS << "        handled = true;\n";
             bridgeOS << "    }\n";
        }

        bridgeOS << "    if (!handled) {\n";
        bridgeOS << "        fprintf(stderr, \"Error: Unknown function method '%s' called\\n\", func_name);\n";
        bridgeOS << "        // TODO: Return error JSON\n";
        bridgeOS << "        result = NULL;\n";
        bridgeOS << "    }\n\n";

         bridgeOS << "    // Free the params object if we allocated it\n";
         bridgeOS << "    if (params_allocated) {\n";
         bridgeOS << "        cJSON_Delete(params_obj);\n";
         bridgeOS << "    }\n\n";
        bridgeOS << "    return result;\n";
        bridgeOS << "}\n\n";

        bridgeOS << "#ifdef __cplusplus\n} // extern \"C\"\n#endif\n";
        bridgeOS.flush();
    }

    // NEW: Generate the per-file bridge C and H files
    void generatePerFileBridgeCode() {
        errs() << "Generating per-file bridge code...\n";
        std::map<std::string, std::unique_ptr<raw_fd_ostream>> c_streams;
        std::map<std::string, std::unique_ptr<raw_fd_ostream>> h_streams;
        std::set<std::string> generated_bases; // Track bases we've already boilerplated

        // Phase 1: Generate Parsers
        for (const auto& [exportName, enumDef] : g_persistentEnums) {
            const std::string& baseName = enumDef.sourceFileBase;
            if (!c_streams.count(baseName)) { // Open streams if not already open
                 if (!openPerFileOutputStreams(baseName, c_streams[baseName], h_streams[baseName])) continue;
                 generatePerFileHeaderBoilerplate(*h_streams[baseName], baseName, enumDef.requiredIncludes);
                 // Include original headers in the generated C file
                 *c_streams[baseName] << "// Generated bridge C file for " << baseName << "\n";
                 *c_streams[baseName] << "#include \"" << baseName << "_bridge.h\"\n";
                  *c_streams[baseName] << "#include \"cJSON.h\"\n";
                  *c_streams[baseName] << "#include <string.h>\n";
                  *c_streams[baseName] << "#include <stdlib.h>\n";
                  *c_streams[baseName] << "#include <stdio.h>\n";
                   *c_streams[baseName] << "#include <stdbool.h>\n\n";
                 *c_streams[baseName] << "// Include original headers required by definitions in " << baseName << "\n";
                 for(const std::string& include : enumDef.requiredIncludes) { // Use includes from first item found for this base
                      *c_streams[baseName] << "#include \"" << include << "\"\n";
                 }
                 *c_streams[baseName] << "\n";
                 generated_bases.insert(baseName);
            }
            generateEnumParser(*c_streams[baseName], *h_streams[baseName], enumDef);
        }
        for (const auto& [exportName, structDef] : g_persistentStructs) {
             const std::string& baseName = structDef.sourceFileBase;
             if (!c_streams.count(baseName)) { // Open streams if not already open
                 if (!openPerFileOutputStreams(baseName, c_streams[baseName], h_streams[baseName])) continue;
                 generatePerFileHeaderBoilerplate(*h_streams[baseName], baseName, structDef.requiredIncludes);
                  // Include original headers in the generated C file
                 *c_streams[baseName] << "// Generated bridge C file for " << baseName << "\n";
                 *c_streams[baseName] << "#include \"" << baseName << "_bridge.h\"\n";
                  *c_streams[baseName] << "#include \"cJSON.h\"\n";
                  *c_streams[baseName] << "#include <string.h>\n";
                  *c_streams[baseName] << "#include <stdlib.h>\n";
                   *c_streams[baseName] << "#include <stdio.h>\n";
                  *c_streams[baseName] << "#include <stdbool.h>\n\n";
                 *c_streams[baseName] << "// Include original headers required by definitions in " << baseName << "\n";
                  // Find includes - need a better way to aggregate includes per file base
                  std::set<std::string> includes_for_base;
                  if(g_persistentStructs.count(exportName)) includes_for_base = g_persistentStructs.at(exportName).requiredIncludes;
                   else if (g_persistentEnums.count(exportName)) includes_for_base = g_persistentEnums.at(exportName).requiredIncludes;
                    else if (g_persistentFunctions.count(exportName)) includes_for_base = g_persistentFunctions.at(exportName).requiredIncludes;


                 for(const std::string& include : includes_for_base) { // Use includes from first item found
                      *c_streams[baseName] << "#include \"" << include << "\"\n";
                 }
                  *c_streams[baseName] << "\n";
                  generated_bases.insert(baseName);
            }
             generateStructParser(*c_streams[baseName], *h_streams[baseName], structDef);
        }

         // Phase 2: Generate Function Handlers (need parsers to be declared first)
         for (const auto& [exportName, funcDef] : g_persistentFunctions) {
             const std::string& baseName = funcDef.sourceFileBase;
              if (!c_streams.count(baseName)) { // Open streams if not already open
                 if (!openPerFileOutputStreams(baseName, c_streams[baseName], h_streams[baseName])) continue;
                 generatePerFileHeaderBoilerplate(*h_streams[baseName], baseName, funcDef.requiredIncludes);
                 // Include original headers in the generated C file
                 *c_streams[baseName] << "// Generated bridge C file for " << baseName << "\n";
                 *c_streams[baseName] << "#include \"" << baseName << "_bridge.h\"\n";
                 *c_streams[baseName] << "#include \"cJSON.h\"\n";
                 *c_streams[baseName] << "#include <string.h>\n";
                 *c_streams[baseName] << "#include <stdlib.h>\n";
                 *c_streams[baseName] << "#include <stdio.h>\n";
                 *c_streams[baseName] << "#include <stdbool.h>\n\n";
                 *c_streams[baseName] << "// Include original headers required by definitions in " << baseName << "\n";
                  for(const std::string& include : funcDef.requiredIncludes) {
                      *c_streams[baseName] << "#include \"" << include << "\"\n";
                 }
                 *c_streams[baseName] << "\n// Forward declare original functions needed\n";
                  *c_streams[baseName] << "extern " << funcDef.returnTypeName << " " << funcDef.originalName << "(";
                    for (size_t i = 0; i < funcDef.parameters.size(); ++i) {
                        *c_streams[baseName] << (i > 0 ? ", " : "") << funcDef.parameters[i].typeName; // No names needed for extern decl
                    }
                 *c_streams[baseName] << ");\n\n";

                 generated_bases.insert(baseName);
             }
             // Include forward declarations of parsers from other files if needed? Complex. Assume headers are included.
             generateFunctionHandler(*c_streams[baseName], *h_streams[baseName], funcDef);
         }

        // Phase 3: Finalize all open per-file streams
         for (const std::string& baseName : generated_bases) {
            generatePerFileFooter(*c_streams[baseName], *h_streams[baseName]);
         }
         errs() << "Finished generating per-file bridge code.\n";
    }


     // Helper function for escaping strings for JSON generation C code
    std::string escapeString(const std::string& input) {
        std::string output;
        output.reserve(input.length());
        for (char c : input) {
            switch (c) {
                case '"':  output += "\\\""; break;
                case '\\': output += "\\\\"; break;
                case '\b': output += "\\b"; break;
                case '\f': output += "\\f"; break;
                case '\n': output += "\\n"; break;
                case '\r': output += "\\r"; break;
                case '\t': output += "\\t"; break;
                default:
                    if (c >= 0 && c < 32) {
                        // Handle other control characters if necessary (e.g., print as \uXXXX)
                        // Simple approach: just skip or replace with space? Skipping for now.
                    } else {
                        output += c;
                    }
                    break;
            }
        }
        return output;
    }

    // NEW: Main function called by destructor after all TUs processed
    void FinalizeGeneration() {
        // 1. Generate Per-File Bridge Code (.c and .h for each source base)
        generatePerFileBridgeCode();

        // 2. Generate Final Signature File (using persistent data)
        std::error_code EC_sig;
        raw_fd_ostream sigOS(SigOutputFilename, EC_sig, llvm::sys::fs::OF_Text);
        if (EC_sig) {
            errs() << "Error opening final signature file " << SigOutputFilename << ": " << EC_sig.message() << "\n";
        } else {
            generateSignaturesAndDefsFile(sigOS);
            errs() << "Successfully wrote signature file: " << SigOutputFilename << "\n";
        }

        // 3. Generate Final Main Bridge File (using persistent data and including per-file headers)
        std::error_code EC_bridge;
        raw_fd_ostream bridgeOS(BridgeOutputFilename, EC_bridge, llvm::sys::fs::OF_Text);
         if (EC_bridge) {
            errs() << "Error opening final bridge file " << BridgeOutputFilename << ": " << EC_bridge.message() << "\n";
        } else {
             generateMainBridgeFile(bridgeOS);
              errs() << "Successfully wrote bridge file: " << BridgeOutputFilename << "\n";
         }

         // Clear persistent data (optional, as program exits soon)
         //g_persistentEnums.clear();
         //g_persistentStructs.clear();
         //g_persistentFunctions.clear();
         //g_processedFileBases.clear();
         g_allRequiredIncludesForSig.clear();
    }

}; // End ExportAction

// --- Frontend Action Factory ---
// MODIFIED: No longer passes streams to the action
class ExportActionFactory : public FrontendActionFactory {
public:
    ExportActionFactory() = default; // Default constructor is fine

    std::unique_ptr<FrontendAction> create() override {
        // Create the action. Final generation happens in its destructor.
        return std::make_unique<ExportAction>();
    }
};

// --- Main Function ---
// MODIFIED: Uses the new factory, doesn't pass streams to factory.
int main(int argc, const char **argv) {
    std::cout << "Starting export tool...\n";
     // Use CommonOptionsParser::create
     auto ExpectedParser = CommonOptionsParser::create(argc, argv, MyToolCategory);
     if (!ExpectedParser) {
         errs() << toString(ExpectedParser.takeError());
         return 1;
     }
     CommonOptionsParser& OptionsParser = ExpectedParser.get();

     // Check required options (already handled by cl::Required)
     if (SigOutputFilename.empty() || BridgeOutputFilename.empty()) {
         errs() << "Error: Both -s (signature output) and -b (bridge output) options are required.\n";
          cl::PrintHelpMessage(); // Print help message
         return 1;
     }

    // Create the tool instance.
    ClangTool Tool(OptionsParser.getCompilations(), OptionsParser.getSourcePathList());

    // Add specific Clang flags if needed (e.g., include paths from command line)
    // Example: Propagate include paths from the compilation database or add custom ones
    // Tool.appendArgumentsAdjuster(getInsertArgumentAdjuster("-I/path/to/includes", ArgumentInsertPosition::BEGIN));

    // Add adjuster for MSVC compatibility if needed
    #ifdef _MSC_VER
        Tool.appendArgumentsAdjuster(
            getInsertArgumentAdjuster("-U_MSC_VER", ArgumentInsertPosition::BEGIN)
        );
    #endif


    // Create the factory. The Action's destructor will handle final generation.
    auto factory = std::make_unique<ExportActionFactory>();

    outs() << "Running ClangTool...\n";
    // Tool.run will process all translation units.
    // For each TU, it creates an ExportAction, which creates an ExportASTConsumer.
    // The consumer runs matchers, populating global persistent data.
    // When Tool.run finishes, the LAST ExportAction created goes out of scope,
    // triggering its destructor, which calls FinalizeGeneration().
    int result = Tool.run(factory.get());

    // Streams are now opened/closed within FinalizeGeneration and per-file generation.

    if (result == 0) {
         errs() << "Tool execution successful.\n";
    } else {
         errs() << "Tool execution failed with result code: " << result << "\n";
    }

    return result;
}