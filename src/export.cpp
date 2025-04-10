#include "clang/AST/ASTContext.h"
#include "clang/AST/RecordLayout.h" // Needed for QualType details potentially
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Lex/PreprocessorOptions.h" // If needed for header includes
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h" // For sys::fs::OF_Text
#include <fstream>
#include <sstream>
#include <memory>
#include <vector>
#include <map>
#include <set> // To keep track of generated parsers

using namespace clang;
using namespace clang::ast_matchers;
using namespace clang::tooling;
using namespace llvm;



static cl::OptionCategory MyToolCategory("MCPC Options");

static cl::opt<std::string> SigOutputFilename(
    "s",
    cl::desc("指定函数签名 (JSON 生成 C 代码) 输出文件名"),
    cl::value_desc("filename"),
    cl::Required, // Make required
    cl::cat(MyToolCategory));

static cl::opt<std::string> BridgeOutputFilename(
    "b",
    cl::desc("指定桥接 (C 代码) 输出文件名"),
    cl::value_desc("filename"),
    cl::Required, // Make required
    cl::cat(MyToolCategory));

// --- Internal Data Structures (Task 1.1) ---

struct FieldInfo {
    std::string name;
    std::string description;
    clang::QualType type;
};

struct ParameterInfo {
    std::string name;
    std::string description;
    clang::QualType type;
    // bool isRequired; // TODO: Implement logic if default values are supported
};

struct EnumConstantInfo {
    std::string name;
    // int64_t value; // If needed
};

struct EnumDefinition {
    std::string exportName;
    std::string originalName; // The C name (e.g., COLOR)
    std::string description;
    std::vector<EnumConstantInfo> constants;
    clang::QualType underlyingType; // Usually int
};

struct StructDefinition {
    std::string exportName;
    std::string originalName; // The C name (e.g., struct person)
    std::string description;
    std::vector<FieldInfo> fields;
};

struct FunctionDefinition {
    std::string exportName;
    std::string originalName; // The C name (e.g., real_get_person)
    std::string description;
    clang::QualType returnType;
    std::vector<ParameterInfo> parameters;
};

// Global storage for parsed definitions
std::map<std::string, EnumDefinition> g_enums;
std::map<std::string, StructDefinition> g_structs;
std::vector<FunctionDefinition> g_functions;
std::set<std::string> g_requiredIncludes; // Headers needed by generated code

// --- Annotation Parser (Task 1.2) ---
std::string getAnnotationValue(const clang::Decl* D, const std::string& annotationPrefix) {
    if (!D || !D->hasAttrs()) {
        return "";
    }
    for (const auto *Attr : D->getAttrs()) {
        if (const auto *Annotate = dyn_cast<AnnotateAttr>(Attr)) {
            llvm::StringRef annotation = Annotate->getAnnotation();
            if (annotation.starts_with(annotationPrefix)) {
                llvm::StringRef value = annotation.drop_front(annotationPrefix.length());
                // Remove surrounding quotes or '#' if present (simple handling)
                 if (value.starts_with("\"") && value.ends_with("\"")) {
                    value = value.drop_front(1).drop_back(1);
                 } else if (value.starts_with("#")) {
                     value = value.drop_front(1); // Handle EXPORT_AS(#x)
                 }
                return value.str();
            }
        }
    }
    return "";
}

std::string getExportName(const clang::NamedDecl* D) {
    std::string exportName = "";
    if(D->hasAttr<AnnotateAttr>()) {
        exportName = getAnnotationValue(D, "EXPORT_AS=");
        if(exportName.empty()) {
            exportName = D->getNameAsString();
        }
    }
    return exportName;
}

// --- Type Analysis & JSON Schema Generation Helper (Task 2.1 refinement) ---
struct JsonSchemaInfo {
    std::string type; // "integer", "string", "boolean", "number", "object", "array"
    std::string ref;  // "#/$defs/..."
    std::unique_ptr<JsonSchemaInfo> items; // For arrays
    bool isEnum = false; // Special flag for enums treated as strings/integers in schema
    std::string enumExportName; // Store enum name if isEnum is true
};

JsonSchemaInfo getJsonSchemaInfoForType(QualType qualType, ASTContext& context) {
    JsonSchemaInfo schema;
    const clang::Type* type = qualType.getCanonicalType().getTypePtr();

    if (type->isBuiltinType()) {
        const clang::BuiltinType* builtin = cast<clang::BuiltinType>(type);
        switch (builtin->getKind()) {
            case BuiltinType::Bool:
                schema.type = "boolean";
                break;
            case BuiltinType::Char_S:
            case BuiltinType::Char_U:
                 // Check if it's actually char*
                 if (qualType->isPointerType() && qualType->getPointeeType()->isCharType()) {
                      schema.type = "string";
                 } else {
                     schema.type = "string"; // Treat single char as string? Or integer? Choose one. Let's go string for simplicity.
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
                schema.type = "integer";
                break;
            case BuiltinType::Float:
            case BuiltinType::Double:
            case BuiltinType::LongDouble:
                schema.type = "number";
                break;
            default:
                schema.type = "string"; // Fallback for unknown builtins
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
             if (recordDecl->isStruct() || recordDecl->isClass()) { // isClass for C++ structs
                 std::string exportName = getExportName(recordDecl);
                 if (!exportName.empty() && g_structs.count(exportName)) {
                     schema.ref = "#/$defs/" + exportName;
                 } else {
                      schema.type = "object"; // Fallback if not exported/found
                      errs() << "Warning: Struct pointer '" << qualType.getAsString() << "' points to non-exported/unknown struct '" << recordDecl->getNameAsString() << "'. Defaulting to object.\n";
                 }
             } else {
                 schema.type = "object"; // Fallback for other record types?
                 errs() << "Warning: Pointer '" << qualType.getAsString() << "' points to unsupported record type. Defaulting to object.\n";
             }
        } else {
            // Could be pointer to int, void*, etc. How to represent? Often string or object.
            schema.type = "object"; // Generic fallback for other pointers
             errs() << "Warning: Pointer type '" << qualType.getAsString() << "' not directly mapped to JSON schema. Defaulting to object.\n";
        }
    } else if (const EnumType* enumType = type->getAs<EnumType>()) {
        const EnumDecl* enumDecl = enumType->getDecl();
        std::string exportName = getExportName(enumDecl);
        errs() << "enumDecl: " << enumDecl->getNameAsString() << " " << "exportName: " << exportName << 
        " " << "g_enums.count(exportName): " << g_enums.count(exportName) << "\n";
        schema.type = "integer"; // Not mather what, type is always int
        if (!exportName.empty() && g_enums.count(exportName)) {
             errs() << "g_enums.count(exportName): " << g_enums.count(exportName) << "\n";
             schema.ref = "#/$defs/" + exportName;
             errs() << "enum ref: " << schema.ref << "\n";
             schema.isEnum = true; // Mark it, although ref takes precedence in schema
             schema.enumExportName = exportName;
         } else {
             errs() << "Warning: Enum type '" << qualType.getAsString() << "' has no EXPORT_AS annotation. Defaulting to integer.\n";
         }
    } else if (const RecordType* recordType = type->getAs<RecordType>()) {
        // Struct passed by value
         const RecordDecl* recordDecl = recordType->getDecl();
         schema.type = "object"; // Not mather what, type is always object
         if (recordDecl->isStruct() || recordDecl->isClass()) {
             std::string exportName = getExportName(recordDecl);

             if (!exportName.empty() && g_structs.count(exportName)) {
                 schema.ref = "#/$defs/" + exportName;
             } else {
                  errs() << "Warning: Struct '" << qualType.getAsString() << "' passed by value is not exported/known. Defaulting to object.\n";
             }
         } else {
             errs() << "Warning: Unsupported record type '" << qualType.getAsString() << "' passed by value. Defaulting to object.\n";
         }
    } else if (type->isArrayType()) {
        schema.type = "array";
        const clang::ArrayType* arrayType = cast<clang::ArrayType>(type);
        clang::QualType elementType = arrayType->getElementType();
        schema.items = std::make_unique<JsonSchemaInfo>(getJsonSchemaInfoForType(elementType, context));
    } else if (const clang::TypedefType* typedefType = type->getAs<clang::TypedefType>()) {
        // Look through the typedef
        return getJsonSchemaInfoForType(typedefType->desugar(), context);
    } else if (const clang::ElaboratedType* elaboratedType = type->getAs<clang::ElaboratedType>()) {
        // Look through elaborated type (e.g., struct MyStruct)
        return getJsonSchemaInfoForType(elaboratedType->getNamedType(), context);
    }
    else {
        schema.type = "object"; // General fallback
        errs() << "Warning: Unsupported type '" << qualType.getAsString() << "' encountered. Defaulting to object.\n";
    }

    return schema;
}

// --- MatchFinder Callback Implementation ---
class ExportMatcher : public MatchFinder::MatchCallback {
     ASTContext *Context;
public:
        void check_and_add_header(const clang::Decl* ED)
        {
            // Add header where this enum is defined
            SourceManager &SM = Context->getSourceManager();
            std::string header = SM.getFilename(ED->getLocation()).str();
            size_t last_slash = header.find_last_of("/\\");
            if(last_slash != std::string::npos) header = header.substr(last_slash + 1);
            if (header.find(".h")!=std::string::npos
                    ||header.find(".hpp")!=std::string::npos
                )
                g_requiredIncludes.insert(header);
        }
     void run(const MatchFinder::MatchResult &Result) override {
         Context = Result.Context; // Capture context on first match
        errs() << "ExportMatcher.run\n";
         // --- Enum Parsing (Task 1.3) ---
         if (const EnumDecl *ED = Result.Nodes.getNodeAs<EnumDecl>("enumDecl")) {
             if (ED->isThisDeclarationADefinition()) {
                 std::string exportName = getExportName(ED);
                 if (!exportName.empty()) {
                     if (g_enums.count(exportName)) return; // Already processed
                     errs() << "Processing Enum: " << ED->getNameAsString() << " (Exported As: " << exportName << ")\n";
                     EnumDefinition enumDef;
                     enumDef.exportName = exportName;
                     enumDef.originalName = ED->getNameAsString(); // Store original name
                     enumDef.description = getAnnotationValue(ED, "DESCRIPTION=");
                     enumDef.underlyingType = ED->getIntegerType();

                     for (const EnumConstantDecl *ECD : ED->enumerators()) {
                         EnumConstantInfo constantInfo;
                         constantInfo.name = ECD->getNameAsString();
                         // constantInfo.value = ECD->getInitVal().getSExtValue(); // If value needed
                         enumDef.constants.push_back(constantInfo);
                     }
                     g_enums[exportName] = enumDef;

                     check_and_add_header(ED);
                 }
             }
         }
         // --- Struct Parsing (Task 1.4) ---
         else if (const RecordDecl *RD = Result.Nodes.getNodeAs<RecordDecl>("structDecl")) {
              if (RD->isStruct() && RD->isThisDeclarationADefinition()) { // Ensure it's a struct definition
                 std::string exportName = getExportName(RD);
                 if (!exportName.empty()) {
                     if (g_structs.count(exportName)) return; // Already processed
                     errs() << "Processing Struct: " << RD->getNameAsString() << " (Exported As: " << exportName << ")\n";
                     StructDefinition structDef;
                     structDef.exportName = exportName;
                     // Get original name carefully, might be anonymous or typedef'd
                     if (RD->getIdentifier()) {
                         structDef.originalName = RD->getNameAsString();
                     } else {
                         // Try to find a typedef name pointing to this anonymous struct
                         // This requires more advanced AST walking or tracking typedefs
                         // For now, use a placeholder or try to construct one
                         structDef.originalName = exportName + "_struct"; // Placeholder
                         errs() << "Warning: Anonymous struct exported as " << exportName << ". Using placeholder C name.\n";
                     }

                     // Check for struct typedef like `typedef struct Person { ... } Person;`
                     // The RecordDecl might not have the name "Person", the TypedefDecl does.
                     // Need a way to link them. A common pattern is the typedef name matches the EXPORT_AS name.
                     // Let's assume the originalName *is* the typedef name if the struct tag is missing
                     // and adjust the struct definition name in the parser generation accordingly.
                       // A more robust approach involves matching TypedefDecls as well.
                       // For simplicity now: If struct has no tag name, assume typedef name = exportName exists.
                      if (!RD->getIdentifier() && RD->getTypedefNameForAnonDecl()) {
                         structDef.originalName = RD->getTypedefNameForAnonDecl()->getNameAsString();
                         errs() << "Info: Found typedef name '" << structDef.originalName << "' for anonymous struct exported as " << exportName << "\n";
                      } else if (!RD->getIdentifier()) {
                           // If still no name, stick to placeholder
                          errs() << "Warning: Could not definitively determine original C name for anonymous struct exported as " << exportName << "\n";
                      }


                      structDef.description = getAnnotationValue(RD, "DESCRIPTION=");

                      for (const FieldDecl *FD : RD->fields()) {
                          FieldInfo fieldInfo;
                          fieldInfo.name = FD->getNameAsString();
                          fieldInfo.description = getAnnotationValue(FD, "DESCRIPTION=");
                          fieldInfo.type = FD->getType();
                          structDef.fields.push_back(fieldInfo);
                      }
                      g_structs[exportName] = structDef;

                     check_and_add_header(RD);
                 }
              }
         }
        // --- Function Parsing (Task 1.5) ---
        else if (const FunctionDecl *FD = Result.Nodes.getNodeAs<FunctionDecl>("exportedFunction")) {
            if (!FD->isThisDeclarationADefinition()) return; // Only process definitions

            std::string exportName = getAnnotationValue(FD, "EXPORT_AS=");
            bool isExported = FD->hasAttr<AnnotateAttr>() && getAnnotationValue(FD, "EXPORT") == "EXPORT"; // Check for EXPORT annotation specifically

            if (!exportName.empty() || isExported) {
                 if (exportName.empty()) {
                     exportName = FD->getNameAsString(); // Use function name if EXPORT but no EXPORT_AS
                 }

                // Avoid duplicates if run multiple times? (Less likely with tool setup)
                // Check if already processed? Need a unique key. exportName should work.
                bool found = false;
                for(const auto& f : g_functions) { if (f.exportName == exportName) { found = true; break; } }
                if(found) return;

                errs() << "Processing Function: " << FD->getNameAsString() << " (Exported As: " << exportName << ")\n";

                FunctionDefinition funcDef;
                funcDef.exportName = exportName;
                funcDef.originalName = FD->getNameAsString();
                funcDef.description = getAnnotationValue(FD, "DESCRIPTION=");
                funcDef.returnType = FD->getReturnType();

                for (unsigned i = 0; i < FD->getNumParams(); ++i) {
                    const ParmVarDecl *PVD = FD->getParamDecl(i);
                    ParameterInfo paramInfo;
                    paramInfo.name = PVD->getNameAsString();
                     if (paramInfo.name.empty()) {
                         // Handle unnamed parameters if necessary, e.g., generate "param1", "param2"
                         paramInfo.name = "param" + std::to_string(i + 1);
                         errs() << "Warning: Unnamed parameter found in function " << funcDef.originalName << ", using generated name '" << paramInfo.name << "'\n";
                     }
                    paramInfo.description = getAnnotationValue(PVD, "DESCRIPTION=");
                    paramInfo.type = PVD->getType();
                    // paramInfo.isRequired = !PVD->hasDefaultArg(); // If default args are supported
                    funcDef.parameters.push_back(paramInfo);
                }
                 g_functions.push_back(funcDef);

                 check_and_add_header(FD);
            }
        }
     }
};

// --- Clang AST Consumer and Matcher Callback ---

class ExportASTConsumer : public ASTConsumer {
    ASTContext *Context;
    raw_fd_ostream &sigOS; // For generated_function_signatures.c
    raw_fd_ostream &bridgeOS; // For generated_bridge_code.c
    std::string inputHeaderFile; // Store the main header file name

     ExportMatcher MatcherCallback; // Matcher needs to live long enough
     MatchFinder Finder;

    // Helper to get the unqualified original type name (e.g., "person" from "struct person")
    std::string getBaseTypeName(QualType qt) {
        QualType canonical = qt.getCanonicalType();
         if (const RecordType *rt = canonical->getAs<RecordType>()) {
             return rt->getDecl()->getNameAsString();
         } else if (const EnumType *et = canonical->getAs<EnumType>()) {
             return et->getDecl()->getNameAsString();
         } else if (const TypedefType *tt = canonical->getAs<TypedefType>()) {
             // For typedefs, might want the typedef name itself if it's simple
             // Or recursively get the underlying base name
             return getBaseTypeName(tt->desugar()); // Simple recursive approach
         }
         // Fallback for basic types or complex types not handled above
         return qt.getAsString();
    }


    // Helper to generate the C code for creating a JSON schema object
    void generateJsonSchemaCCode(raw_fd_ostream &os, const JsonSchemaInfo& schemaInfo, const std::string& parentVar, const std::string& keyVar) {
        std::string schemaVar = keyVar + "_schema"; // e.g., "param_id_schema"
        os << "        cJSON* " << schemaVar << " = cJSON_CreateObject();\n";
        os << "        if (" << schemaVar << ") {\n"; // Check creation

        if (!schemaInfo.ref.empty()) {
            os << "            cJSON_AddStringToObject(" << schemaVar << ", \"$ref\", \"" << schemaInfo.ref << "\");\n";
        } else if (schemaInfo.type == "array" && schemaInfo.items) {
            os << "            cJSON_AddStringToObject(" << schemaVar << ", \"type\", \"array\");\n";
            // Recursively generate items schema
            std::string itemsKey = keyVar + "_items";
            generateJsonSchemaCCode(os, *schemaInfo.items, schemaVar, itemsKey); // Parent is schemaVar, key is "items"
        } else {
             os << "            cJSON_AddStringToObject(" << schemaVar << ", \"type\", \"" << schemaInfo.type << "\");\n";
             // Handle enum values if it's an enum *not* using $ref (e.g., directly defined)
             // This case might not be needed if we always use $ref for exported enums
        }
        // Add description if available (needs to be passed down or looked up)
        // os << "            // Add description here if available\n";

        // Add the generated schema object to its parent
        if (parentVar.find("properties") != std::string::npos || parentVar.find("defs") != std::string::npos) { // Adding to properties/defs object
             os << "            cJSON_AddItemToObject(" << parentVar << ", " << keyVar << ", " << schemaVar << ");\n";
        } else if (schemaInfo.type == "array") { // Adding items schema to an array schema
             os << "            cJSON_AddItemToObject(" << parentVar << ", \"items\", " << schemaVar << ");\n";
        }
         os << "        }\n"; // End if (schemaVar)
    }


     // Generate the `get_all_function_signatures_json` function (Tasks 2.2, 2.3)
    void generateSignaturesAndDefsFile() {
        errs() << "Generating signatures and defs file\n";
        sigOS << "// 函数签名 JSON 生成代码 (自动生成，请勿修改)\n";
        sigOS << "#include \"cJSON.h\"\n";
        sigOS << "#include \"string.h\" // For strcmp in potential enum parsing fallback\n\n";
        // Include necessary C headers (struct/enum definitions)
        for(const std::string& include : g_requiredIncludes) {
             sigOS << "#include \"" << include << "\"\n";
        }
        sigOS << "\n";

        sigOS << "#ifdef __cplusplus\nextern \"C\" {\n#endif\n\n";

        sigOS << "cJSON* get_all_function_signatures_json() {\n";
        sigOS << "    cJSON* root = cJSON_CreateObject();\n";
        sigOS << "    if (!root) return NULL;\n\n";

        // --- Generate $defs (Task 2.2) ---
        sigOS << "    // --- $defs --- \n";
        sigOS << "    cJSON* defs = cJSON_CreateObject();\n";
        sigOS << "    if (!defs) { cJSON_Delete(root); return NULL; }\n";
        sigOS << "    cJSON_AddItemToObject(root, \"$defs\", defs);\n\n";

        // $defs for Enums
        for (const auto& [exportName, enumDef] : g_enums) {
            sigOS << "    // Definition for enum: " << exportName << "\n";
            sigOS << "    {\n";
            sigOS << "        cJSON* enum_def = cJSON_CreateObject();\n";
            sigOS << "        if (enum_def) {\n";
            if (!enumDef.description.empty()) {
                sigOS << "            cJSON_AddStringToObject(enum_def, \"description\", \"" << enumDef.description << "\");\n";
            }
             // Use underlying type for JSON schema type
             JsonSchemaInfo typeInfo = getJsonSchemaInfoForType(enumDef.underlyingType, *Context);
            sigOS << "            cJSON_AddStringToObject(enum_def, \"type\", \"" << typeInfo.type << "\"); // Assuming underlying type maps well\n";

            sigOS << "            cJSON* enum_values = cJSON_CreateArray();\n";
            sigOS << "            if (enum_values) {\n";
            for (const auto& constant : enumDef.constants) {
                sigOS << "                cJSON_AddItemToArray(enum_values, cJSON_CreateString(\"" << constant.name << "\"));\n";
            }
            sigOS << "                cJSON_AddItemToObject(enum_def, \"enum\", enum_values);\n";
            sigOS << "            }\n"; // end if enum_values
            sigOS << "            cJSON_AddItemToObject(defs, \"" << exportName << "\", enum_def);\n";
             sigOS << "        } // end if enum_def\n";
            sigOS << "    }\n\n";
        }

        // $defs for Structs
        for (const auto& [exportName, structDef] : g_structs) {
            sigOS << "    // Definition for struct: " << exportName << "\n";
            sigOS << "    {\n";
            sigOS << "        cJSON* struct_def = cJSON_CreateObject();\n";
             sigOS << "        if (struct_def) {\n";
            if (!structDef.description.empty()) {
                sigOS << "            cJSON_AddStringToObject(struct_def, \"description\", \"" << structDef.description << "\");\n";
            }
            sigOS << "            cJSON_AddStringToObject(struct_def, \"type\", \"object\");\n";
            sigOS << "            cJSON* properties = cJSON_CreateObject();\n";
            sigOS << "            if (properties) {\n";
             sigOS << "                cJSON* required_props = cJSON_CreateArray(); // Structs usually have all properties required unless optional pointers\n";
             sigOS << "                if(required_props) {\n";

            for (const auto& field : structDef.fields) {
                sigOS << "                // Field: " << field.name << "\n";
                JsonSchemaInfo schemaInfo = getJsonSchemaInfoForType(field.type, *Context);
                std::string fieldSchemaVar = exportName + "_" + field.name + "_schema";
                std::string fieldKey = "\"" + field.name + "\"";

                sigOS << "                cJSON* " << fieldSchemaVar << " = cJSON_CreateObject();\n";
                 sigOS << "                if (" << fieldSchemaVar << ") {\n";

                 if (!schemaInfo.ref.empty()) {
                     sigOS << "                    cJSON_AddStringToObject(" << fieldSchemaVar << ", \"$ref\", \"" << schemaInfo.ref << "\");\n";
                 } else if (schemaInfo.type == "array" && schemaInfo.items) {
                     sigOS << "                    cJSON_AddStringToObject(" << fieldSchemaVar << ", \"type\", \"array\");\n";
                     std::string itemsSchemaVar = fieldSchemaVar + "_items";
                     std::string itemsKeyStr = "\"items\"";
                     // Generate items schema C code
                     sigOS << "                    cJSON* " << itemsSchemaVar << " = cJSON_CreateObject();\n";
                      sigOS << "                    if (" << itemsSchemaVar << ") {\n";
                      if (!schemaInfo.items->ref.empty()) {
                          sigOS << "                        cJSON_AddStringToObject(" << itemsSchemaVar << ", \"$ref\", \"" << schemaInfo.items->ref << "\");\n";
                      } else {
                          sigOS << "                        cJSON_AddStringToObject(" << itemsSchemaVar << ", \"type\", \"" << schemaInfo.items->type << "\");\n";
                      }
                        sigOS << "                        cJSON_AddItemToObject(" << fieldSchemaVar << ", " << itemsKeyStr << ", " << itemsSchemaVar << ");\n";
                       sigOS << "                    }\n"; // end if itemsSchemaVar
                 } else {
                     sigOS << "                    cJSON_AddStringToObject(" << fieldSchemaVar << ", \"type\", \"" << schemaInfo.type << "\");\n";
                 }

                 if (!field.description.empty()) {
                    sigOS << "                    cJSON_AddStringToObject(" << fieldSchemaVar << ", \"description\", \"" << field.description << "\");\n";
                 }

                 sigOS << "                    cJSON_AddItemToObject(properties, " << fieldKey << ", " << fieldSchemaVar << ");\n";
                 // Assume all struct fields are required for now unless they are pointers? Needs refinement.
                 sigOS << "                    cJSON_AddItemToArray(required_props, cJSON_CreateString(" << fieldKey << "));\n";
                 sigOS << "                }\n"; // end if fieldSchemaVar
            }
            sigOS << "                    cJSON_AddItemToObject(struct_def, \"properties\", properties);\n";
            sigOS << "                    cJSON_AddItemToObject(struct_def, \"required\", required_props);\n";
            sigOS << "                 } else { cJSON_Delete(properties); } // end if required_props\n"; // Cleanup if required fails
            sigOS << "            } // end if properties\n";
            sigOS << "            cJSON_AddItemToObject(defs, \"" << exportName << "\", struct_def);\n";
             sigOS << "        } // end if struct_def\n";
            sigOS << "    }\n\n";
        }


        // --- Generate tools (Task 2.3) ---
        sigOS << "    // --- tools --- \n";
        sigOS << "    cJSON* tools = cJSON_CreateArray();\n";
        sigOS << "    if (!tools) { cJSON_Delete(root); return NULL; }\n";
        sigOS << "    cJSON_AddItemToObject(root, \"tools\", tools);\n\n";

        for (const auto& funcDef : g_functions) {
            sigOS << "    // Tool for function: " << funcDef.exportName << "\n";
            sigOS << "    {\n";
            sigOS << "        cJSON* tool = cJSON_CreateObject();\n";
            sigOS << "        if (tool) {\n";
            sigOS << "            cJSON_AddStringToObject(tool, \"name\", \"" << funcDef.exportName << "\");\n";
            if (!funcDef.description.empty()) {
                sigOS << "            cJSON_AddStringToObject(tool, \"description\", \"" << funcDef.description << "\");\n";
            } else {
                 sigOS << "            cJSON_AddStringToObject(tool, \"description\", \"\"); // Add empty description if none provided\n";
            }

            sigOS << "            cJSON* inputSchema = cJSON_CreateObject();\n";
            sigOS << "            if (inputSchema) {\n";
            sigOS << "                cJSON_AddStringToObject(inputSchema, \"type\", \"object\");\n";
            sigOS << "                cJSON* properties = cJSON_CreateObject();\n";
            sigOS << "                cJSON* required = cJSON_CreateArray();\n";

            sigOS << "                if (properties && required) {\n";
            for (const auto& param : funcDef.parameters) {
                 sigOS << "                // Parameter: " << param.name << "\n";
                 JsonSchemaInfo schemaInfo = getJsonSchemaInfoForType(param.type, *Context);
                 std::string paramSchemaVar = funcDef.exportName + "_" + param.name + "_schema";
                 std::string paramKey = "\"" + param.name + "\"";

                 sigOS << "                cJSON* " << paramSchemaVar << " = cJSON_CreateObject();\n";
                  sigOS << "                if (" << paramSchemaVar << ") {\n";

                  if (!schemaInfo.ref.empty()) {
                      sigOS << "                    cJSON_AddStringToObject(" << paramSchemaVar << ", \"$ref\", \"" << schemaInfo.ref << "\");\n";
                  } else if (schemaInfo.type == "array" && schemaInfo.items) {
                      sigOS << "                    cJSON_AddStringToObject(" << paramSchemaVar << ", \"type\", \"array\");\n";
                      std::string itemsSchemaVar = paramSchemaVar + "_items";
                      std::string itemsKeyStr = "\"items\"";
                      // Generate items schema C code
                      sigOS << "                    cJSON* " << itemsSchemaVar << " = cJSON_CreateObject();\n";
                       sigOS << "                    if (" << itemsSchemaVar << ") {\n";
                       if (!schemaInfo.items->ref.empty()) {
                           sigOS << "                        cJSON_AddStringToObject(" << itemsSchemaVar << ", \"$ref\", \"" << schemaInfo.items->ref << "\");\n";
                       } else {
                           sigOS << "                        cJSON_AddStringToObject(" << itemsSchemaVar << ", \"type\", \"" << schemaInfo.items->type << "\");\n";
                       }
                        sigOS << "                        cJSON_AddItemToObject(" << paramSchemaVar << ", " << itemsKeyStr << ", " << itemsSchemaVar << ");\n";
                       sigOS << "                    }\n"; // end if itemsSchemaVar
                  } else {
                      sigOS << "                    cJSON_AddStringToObject(" << paramSchemaVar << ", \"type\", \"" << schemaInfo.type << "\");\n";
                  }

                  if (!param.description.empty()) {
                     sigOS << "                    cJSON_AddStringToObject(" << paramSchemaVar << ", \"description\", \"" << param.description << "\");\n";
                  }
                  // Add default value if available (TODO)
                  // if (param.hasDefault) { ... cJSON_Add...ToObject(paramSchemaVar, "default", ...); }

                  sigOS << "                    cJSON_AddItemToObject(properties, " << paramKey << ", " << paramSchemaVar << ");\n";
                  // Assume required if no default (TODO: refine this logic)
                  sigOS << "                    cJSON_AddItemToArray(required, cJSON_CreateString(" << paramKey << "));\n";
                  sigOS << "                 }\n"; // end if paramSchemaVar
            }
            sigOS << "                    cJSON_AddItemToObject(inputSchema, \"properties\", properties);\n";
            sigOS << "                    cJSON_AddItemToObject(inputSchema, \"required\", required);\n";
             sigOS << "                } else { \n"; // Cleanup if properties/required alloc fails
             sigOS << "                    if (properties) cJSON_Delete(properties);\n";
             sigOS << "                    if (required) cJSON_Delete(required);\n";
             sigOS << "                    cJSON_Delete(inputSchema);\n"; // Delete schema if internals fail
             sigOS << "                    inputSchema = NULL;\n";
             sigOS << "                }\n"; // end if properties && required

             sigOS << "                 if (inputSchema) {\n"; // Check again before adding
             sigOS << "                     cJSON_AddFalseToObject(inputSchema, \"additionalProperties\");\n";
             sigOS << "                     cJSON_AddStringToObject(inputSchema, \"$schema\", \"http://json-schema.org/draft-07/schema#\");\n";
             sigOS << "                     cJSON_AddItemToObject(tool, \"inputSchema\", inputSchema);\n";
             sigOS << "                 }\n";
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
        sigOS.flush(); // Ensure content is written
    }


    // --- Bridge Code Generation (Phase 3) ---

    // Generate parser for a specific Enum (Task 3.2)
    void generateEnumParser(const EnumDefinition& enumDef) {
        bridgeOS << "// Parser for enum " << enumDef.exportName << "\n";
        // Use original C enum type name in the function signature
        bridgeOS << "static inline " << enumDef.originalName << " parse_" << enumDef.exportName << "(cJSON *json) {\n";
        bridgeOS << "    if (!json) return (" << enumDef.originalName << ")0; // Or a default/error value\n";
        bridgeOS << "    if (cJSON_IsString(json)) {\n";
        bridgeOS << "        const char* str = json->valuestring;\n";
        for (size_t i = 0; i < enumDef.constants.size(); ++i) {
            const auto& constant = enumDef.constants[i];
            bridgeOS << "        " << (i > 0 ? "else if" : "if") << " (strcmp(str, \"" << constant.name << "\") == 0) {\n";
            // Use the original C constant name here
            bridgeOS << "            return " << constant.name << ";\n";
             bridgeOS << "        }\n";
        }
        bridgeOS << "        else {\n";
        bridgeOS << "            // Handle error: unknown string value\n";
        bridgeOS << "            return (" << enumDef.originalName << ")0; // Default/error value\n";
        bridgeOS << "        }\n";
        bridgeOS << "    } else if (cJSON_IsNumber(json)) {\n";
        // Assuming the number corresponds directly to the enum value
        bridgeOS << "        return (" << enumDef.originalName << ")json->valueint;\n";
        bridgeOS << "    } else {\n";
        bridgeOS << "         // Handle error: unexpected JSON type\n";
         bridgeOS << "        return (" << enumDef.originalName << ")0; // Default/error value\n";
        bridgeOS << "    }\n";
        bridgeOS << "}\n\n";
    }

    // Generate parser for a specific Struct (Task 3.1)
    void generateStructParser(const StructDefinition& structDef) {
        bridgeOS << "// Parser for struct " << structDef.exportName << "\n";
         // Use original C struct type name
        bridgeOS << "static inline struct " << structDef.originalName << "* parse_" << structDef.exportName << "(cJSON *json) {\n";
        bridgeOS << "    if (!json || !cJSON_IsObject(json)) return NULL;\n";
         // Use original C struct type name for malloc
        bridgeOS << "    struct " << structDef.originalName << "* obj = (struct " << structDef.originalName << "*)malloc(sizeof(struct " << structDef.originalName << "));\n";
        bridgeOS << "    if (!obj) return NULL;\n";
        bridgeOS << "    memset(obj, 0, sizeof(struct " << structDef.originalName << ")); // Initialize memory\n\n";
        bridgeOS << "    cJSON* field_json = NULL;\n";
        for (const auto& field : structDef.fields) {
            bridgeOS << "    // Field: " << field.name << "\n";

            bridgeOS << "    field_json = cJSON_GetObjectItem(json, \"" << field.name << "\");\n";
            bridgeOS << "    if (field_json && !cJSON_IsNull(field_json)) { // Check field exists and is not null\n";

            QualType fieldType = field.type.getCanonicalType();
            const clang::Type* typePtr = fieldType.getTypePtr();

            if (typePtr->isBuiltinType() || (typePtr->isPointerType() && typePtr->getPointeeType()->isCharType())) {
                 JsonSchemaInfo schema = getJsonSchemaInfoForType(field.type, *Context); // Re-use schema info logic
                 if (schema.type == "string") {
                     bridgeOS << "        if (cJSON_IsString(field_json)) {\n";
                     bridgeOS << "            obj->" << field.name << " = strdup(field_json->valuestring);\n";
                      bridgeOS << "           // TODO: Remember to free this string later!\n";
                     bridgeOS << "        }\n";
                 } else if (schema.type == "integer") {
                     bridgeOS << "        if (cJSON_IsNumber(field_json)) {\n";
                     bridgeOS << "            obj->" << field.name << " = field_json->valueint;\n";
                     bridgeOS << "        }\n";
                 } else if (schema.type == "boolean") {
                      bridgeOS << "        if (cJSON_IsBool(field_json)) {\n";
                      bridgeOS << "            obj->" << field.name << " = cJSON_IsTrue(field_json);\n";
                      bridgeOS << "        }\n";
                 } else if (schema.type == "number") {
                     bridgeOS << "        if (cJSON_IsNumber(field_json)) {\n";
                     bridgeOS << "            obj->" << field.name << " = field_json->valuedouble;\n";
                     bridgeOS << "        }\n";
                 }
            } else if (const EnumType* enumType = typePtr->getAs<EnumType>()) {
                 std::string enumExportName = getAnnotationValue(enumType->getDecl(), "EXPORT_AS=");
                 if (!enumExportName.empty()) {
                     bridgeOS << "        obj->" << field.name << " = parse_" << enumExportName << "(field_json);\n";
                 } else {
                     bridgeOS << "        // Warning: Cannot parse non-exported enum field '" << field.name << "'\n";
                 }
            } else if (const RecordType* recordType = typePtr->getAs<RecordType>()) {
                // Struct by value
                 std::string structExportName = getAnnotationValue(recordType->getDecl(), "EXPORT_AS=");
                 if (!structExportName.empty()) {
                      // Use original C type name for temp variable
                     std::string originalFieldTypeName = getBaseTypeName(field.type);
                     bridgeOS << "        struct " << originalFieldTypeName << "* temp_" << field.name << " = parse_" << structExportName << "(field_json);\n";
                     bridgeOS << "        if (temp_" << field.name << ") {\n";
                     bridgeOS << "            obj->" << field.name << " = *temp_" << field.name << "; // Copy value\n";
                     bridgeOS << "            free(temp_" << field.name << "); // Free temporary parsed object\n";
                     bridgeOS << "        }\n";
                 } else {
                      bridgeOS << "        // Warning: Cannot parse non-exported struct value field '" << field.name << "'\n";
                 }
            } else if (typePtr->isPointerType()) {
                 QualType pointeeType = typePtr->getPointeeType();
                 if (const RecordType* pointeeRecord = pointeeType.getCanonicalType()->getAs<RecordType>()) {
                     // Pointer to struct
                      std::string structExportName = getAnnotationValue(pointeeRecord->getDecl(), "EXPORT_AS=");
                      if (!structExportName.empty()) {
                          bridgeOS << "        obj->" << field.name << " = parse_" << structExportName << "(field_json);\n";
                           bridgeOS << "       // Note: Caller of parse_" << structDef.exportName << " might need to free this pointer\n";
                      } else {
                           bridgeOS << "        // Warning: Cannot parse pointer to non-exported struct field '" << field.name << "'\n";
                           bridgeOS << "        obj->" << field.name << " = NULL;\n";
                      }
                 } else {
                      bridgeOS << "        // Warning: Unsupported pointer type for field '" << field.name << "'\n";
                      bridgeOS << "        obj->" << field.name << " = NULL;\n";
                 }
            } else if (typePtr->isArrayType()) {
                bridgeOS << "        // TODO: Implement array parsing for field '" << field.name << "'\n";
            }
             else {
                 bridgeOS << "        // Warning: Unsupported type for field '" << field.name << "'\n";
            }

            bridgeOS << "    } else {\n";
            bridgeOS << "        // Field missing or null in JSON, obj->" << field.name << " remains initialized (likely 0/NULL)\n";
            bridgeOS << "    }\n";
        }

        bridgeOS << "\n    return obj;\n";
        bridgeOS << "}\n\n";
    }

    // Generate the main bridge function (Task 3.3)
    void generateBridgeFunction() {
        bridgeOS << "// --- Main Bridge Function --- \n";
        bridgeOS << "cJSON* bridge(cJSON* input_json) {\n";
        bridgeOS << "    if (!input_json) return NULL;\n"; // Basic validation
        bridgeOS << "    cJSON* func_item = cJSON_GetObjectItem(input_json, \"func\");\n";
        bridgeOS << "    cJSON* param_item = cJSON_GetObjectItem(input_json, \"param\");\n\n";
        bridgeOS << "    if (!func_item || !cJSON_IsString(func_item) || !param_item || !cJSON_IsObject(param_item)) {\n";
        bridgeOS << "        // Invalid input format\n";
        bridgeOS << "        // TODO: Return error JSON?\n";
        bridgeOS << "        return NULL;\n";
        bridgeOS << "    }\n\n";
        bridgeOS << "    const char* func_name = func_item->valuestring;\n";
        bridgeOS << "    cJSON* result = NULL;\n\n";
        // Keep track of allocated memory that needs freeing after the call
        bridgeOS << "    // Memory to free after function call\n";
        bridgeOS << "    #define MAX_ALLOCS 10 // Adjust as needed\n";
        bridgeOS << "    void* allocations[MAX_ALLOCS];\n";
        bridgeOS << "    int alloc_count = 0;\n\n";

        bridgeOS << "    if (0) {\n";
        for (size_t i = 0; i < g_functions.size(); ++i) {
            const auto& funcDef = g_functions[i];
            bridgeOS << "    } else if (strcmp(func_name, \"" << funcDef.exportName << "\") == 0) {\n";
            // Declare parameter variables
            for (const auto& param : funcDef.parameters) {
                // Use original C type name for variable declaration
                 bridgeOS << "        " << param.type.getAsString() << " p_" << param.name << " = {0}; // Initialize\n";
            }
             bridgeOS << "\n";

            // Parse parameters
             bridgeOS << "        // Parse parameters\n";
            bool parsing_ok = true; // Simple flag for now
            bridgeOS << "        cJSON* p_json = NULL;\n";
            for (const auto& param : funcDef.parameters) {
                bridgeOS << "        p_json = cJSON_GetObjectItem(param_item, \"" << param.name << "\");\n";
                // TODO: Add check if parameter is required and p_json is NULL
                bridgeOS << "        if (p_json && !cJSON_IsNull(p_json)&& alloc_count < MAX_ALLOCS) {\n";

                 QualType paramType = param.type.getCanonicalType();
                 const clang::Type* typePtr = paramType.getTypePtr();

                 if (typePtr->isBuiltinType() || (typePtr->isPointerType() && typePtr->getPointeeType()->isCharType())) {
                      JsonSchemaInfo schema = getJsonSchemaInfoForType(param.type, *Context);
                      if (schema.type == "string") {
                          bridgeOS << "            if (cJSON_IsString(p_json)) {\n";
                           bridgeOS << "                p_" << param.name << " = strdup(p_json->valuestring);\n";
                           bridgeOS << "                if (p_" << param.name << " ) allocations[alloc_count++] = p_" << param.name << ";\n"; // Track allocation
                           bridgeOS << "            }\n";
                      } else if (schema.type == "integer") {
                           bridgeOS << "            if (cJSON_IsNumber(p_json)) p_" << param.name << " = p_json->valueint;\n";
                      } else if (schema.type == "boolean") {
                            bridgeOS << "            if (cJSON_IsBool(p_json)) p_" << param.name << " = cJSON_IsTrue(p_json);\n";
                      } else if (schema.type == "number") {
                           bridgeOS << "            if (cJSON_IsNumber(p_json)) p_" << param.name << " = p_json->valuedouble;\n";
                      }
                 } else if (const EnumType* enumType = typePtr->getAs<EnumType>()) {
                      std::string enumExportName = getExportName(enumType->getDecl());
                      if (!enumExportName.empty()) {
                          bridgeOS << "            p_" << param.name << " = parse_" << enumExportName << "(p_json);\n";
                      }
                 } else if (typePtr->isPointerType() && typePtr->getPointeeType().getCanonicalType()->getAs<RecordType>()) {
                     // Pointer to struct
                     QualType pointeeType = typePtr->getPointeeType();
                      const RecordDecl* recordDecl = pointeeType.getCanonicalType()->getAs<RecordType>()->getDecl();
                      std::string structExportName = getExportName(recordDecl);
                      if (!structExportName.empty()) {
                          bridgeOS << "            p_" << param.name << " = parse_" << structExportName << "(p_json);\n";
                           bridgeOS << "           if (p_" << param.name << " && alloc_count < MAX_ALLOCS) allocations[alloc_count++] = p_" << param.name << ";\n"; // Track allocation
                      }
                 } else if (typePtr->isArrayType()) {
                     bridgeOS << "            // TODO: Array parameter parsing for " << param.name << "\n";
                 }
                  else {
                     bridgeOS << "            // Warning: Unsupported parameter type for " << param.name << "\n";
                 }

                bridgeOS << "        } else {\n";
                 bridgeOS << "           // Parameter " << param.name << " missing or null\n";
                 bridgeOS << "           fprintf(stderr, \"Parameter "<<param.name<<" missing or null\");\n";
                 // TODO: Check if required, set parsing_ok = false if so
                 bridgeOS << "        }\n";
            }
             bridgeOS << "\n";

            // Call the original function
             bridgeOS << "        // Call original function\n";
             bridgeOS << "        result = " << funcDef.originalName << "(";
            for (size_t p_idx = 0; p_idx < funcDef.parameters.size(); ++p_idx) {
                bridgeOS << (p_idx > 0 ? ", " : "") << "p_" << funcDef.parameters[p_idx].name;
            }
            bridgeOS << ");\n";
        }
        bridgeOS << "    }\n";
            // Free allocated memory
        bridgeOS << "\n        // Free allocated memory for parameters\n";
        bridgeOS << "    for (int i = 0; i < alloc_count; ++i) {\n";
        bridgeOS << "        if (allocations[i]) free(allocations[i]);\n";


        if (!g_functions.empty()) {
            bridgeOS << "    }\n"; // Close the last else if block
        }

        bridgeOS << "\n    return result;\n";
        bridgeOS << "}\n\n";

        bridgeOS.flush();
    }

     // Generate the includes and boilerplate for the bridge file
    void generateBridgeFileBoilerplate() {
        bridgeOS << "// 桥接代码 (自动生成，请勿修改)\n";
        bridgeOS << "#include \"cJSON.h\"\n";
        bridgeOS << "#include <string.h>\n";
        bridgeOS << "#include <stdlib.h>\n";
        bridgeOS << "#include <stdio.h> // For NULL, potentially error messages\n";
        // Include necessary C headers (struct/enum definitions)
        for(const std::string& include : g_requiredIncludes) {
             bridgeOS << "#include \"" << include << "\"\n";
        }
        bridgeOS << "\n";

        bridgeOS << "#ifdef __cplusplus\nextern \"C\" {\n#endif\n\n";

        // External function declarations
        bridgeOS << "// --- External Function Declarations --- \n";
        for (const auto& funcDef : g_functions) {
             bridgeOS << "extern " << funcDef.returnType.getAsString() << " " << funcDef.originalName << "(";
             for (size_t i = 0; i < funcDef.parameters.size(); ++i) {
                 bridgeOS << (i > 0 ? ", " : "") << funcDef.parameters[i].type.getAsString() << " " << funcDef.parameters[i].name;
             }
             bridgeOS << ");\n";
        }
        bridgeOS << "\n";
    }

     void finalizeBridgeFile() {
        bridgeOS << "\n#ifdef __cplusplus\n} // extern \"C\"\n#endif\n";
        bridgeOS.flush();
     }


public:
    ExportASTConsumer(raw_fd_ostream &sigOS_, raw_fd_ostream &bridgeOS_, ASTContext* Ctx)
        : sigOS(sigOS_), bridgeOS(bridgeOS_), Context(Ctx) {
            errs() << "ExportASTConsumer.ctor - Setting up matchers\n";
        // Define Matchers here, associating them with MatcherCallback
        Finder.addMatcher(
            enumDecl(
                // Keep your matcher conditions: hasAttr(attr::Annotate), isDefinition()
                // Note: hasAttr only checks for *existence*. The value check happens in run().
                // Consider adding `hasAttrWithArgs` if you *only* want to match specific annotations,
                // but your current approach of checking in run() is also fine.
                allOf(
                    hasAttr(attr::Annotate),
                    isDefinition()
                )
            ).bind("enumDecl"),
            &MatcherCallback // Pass the address of the member callback
        );
        Finder.addMatcher(
            recordDecl(
                allOf(
                    isStruct(),
                    hasAttr(attr::Annotate),
                    isDefinition()
                )
            ).bind("structDecl"),
            &MatcherCallback
        );
        Finder.addMatcher(
            functionDecl(
                allOf(
                    hasAttr(attr::Annotate),
                    isDefinition()
                )
            ).bind("exportedFunction"),
            &MatcherCallback
        );

        }

    void HandleTranslationUnit(ASTContext &Ctx) override {
        errs() << "ExportASTConsumer::HandleTranslationUnit - Running MatchFinder...\n";
        // *** RUN THE MATCHER HERE ***
        Finder.matchAST(Ctx);
        errs() << "ExportASTConsumer::HandleTranslationUnit - MatchFinder finished.\n";
            
         // Store the header file name derived from the main source file
         SourceManager &SM = Ctx.getSourceManager();
         FileID MainFileID = SM.getMainFileID();
         const FileEntry *MainFile = SM.getFileEntryForID(MainFileID);
         if (MainFile) {
            StringRef MainFileName = MainFile->tryGetRealPathName();
             if (!MainFileName.empty()) {
                 std::string headerPath = MainFileName.str();
                 size_t last_dot = headerPath.rfind('.');
                 if (last_dot != std::string::npos) {
                     headerPath.replace(last_dot, headerPath.length() - last_dot, ".h");
                     // Get just the filename part for include statement
                     size_t last_slash = headerPath.find_last_of("/\\");
                      if (last_slash != std::string::npos) {
                          inputHeaderFile = headerPath.substr(last_slash + 1);
                      } else {
                          inputHeaderFile = headerPath;
                      }
                      g_requiredIncludes.insert(inputHeaderFile); // Add to includes
                      errs() << "Detected input header: " << inputHeaderFile << "\n";
                 }
             }
         }


        // AST Traversal is done implicitly by Clang before this point
        // Match results are collected in the MatchFinder callbacks

        // Now generate the output files based on collected data
        errs() << "Generating output files...\n";
        errs() << "Found " << g_enums.size() << " enums, "
               << g_structs.size() << " structs, "
               << g_functions.size() << " functions.\n";

        // Generate Signatures/Defs File (`generated_function_signatures.c`)
        generateSignaturesAndDefsFile();

        // Generate Bridge File (`generated_bridge_code.c`)
        generateBridgeFileBoilerplate();
        // Generate parsers first as they are needed by the main bridge function
        for (const auto& [exportName, enumDef] : g_enums) {
            generateEnumParser(enumDef);
        }
        for (const auto& [exportName, structDef] : g_structs) {
            generateStructParser(structDef);
        }
        generateBridgeFunction();
        finalizeBridgeFile();

        errs() << "Output file generation complete.\n";
    }
};




// --- Frontend Action ---
class ExportAction : public ASTFrontendAction {
     raw_fd_ostream &sigOS;
     raw_fd_ostream &bridgeOS;
public:
    ExportAction(raw_fd_ostream &sigOS_, raw_fd_ostream &bridgeOS_) : sigOS(sigOS_), bridgeOS(bridgeOS_) {
        errs() << "ExportAction.ctor\n";
    }

    std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI, StringRef InFile) override {
        errs() << "ExportAction.CreateASTConsumer\n";
        // The consumer itself does the final generation after traversal
        return std::make_unique<ExportASTConsumer>(sigOS, bridgeOS, &CI.getASTContext());
    }
};

// 添加 ExportActionFactory 类定义
class ExportActionFactory : public FrontendActionFactory {
    raw_fd_ostream &sigOS;
    raw_fd_ostream &bridgeOS;
public:
    ExportActionFactory(raw_fd_ostream &sigOS_, raw_fd_ostream &bridgeOS_)
        : sigOS(sigOS_), bridgeOS(bridgeOS_) {}

    std::unique_ptr<FrontendAction> create() override {
        return std::make_unique<ExportAction>(sigOS, bridgeOS);
    }
};

// --- Main Function ---
int main(int argc, const char **argv) {
     // Use CommonOptionsParser::create
     auto ExpectedParser = CommonOptionsParser::create(argc, argv, MyToolCategory);
     if (!ExpectedParser) {
         errs() << toString(ExpectedParser.takeError());
         return 1;
     }
     CommonOptionsParser& OptionsParser = ExpectedParser.get();


    // Command line options already checked for requirement by cl::Required

    // Open output files
    std::error_code EC;
    raw_fd_ostream sigOS(SigOutputFilename, EC, llvm::sys::fs::OF_Text);
    if (EC) {
        errs() << "Error opening signature file " << SigOutputFilename << ": " << EC.message() << "\n";
        return 1;
    }
    raw_fd_ostream bridgeOS(BridgeOutputFilename, EC, llvm::sys::fs::OF_Text);
     if (EC) {
        errs() << "Error opening bridge file " << BridgeOutputFilename << ": " << EC.message() << "\n";
        return 1;
    }

    // Create and run the tool
    ClangTool Tool(OptionsParser.getCompilations(), OptionsParser.getSourcePathList());

    // Add specific flags if needed (e.g., include paths, defines)
    // Tool.appendArgumentsAdjuster(getInsertArgumentAdjuster("-I/path/to/includes", ArgumentInsertPosition::BEGIN));

    // Use the custom FrontendActionFactory
    auto factory = std::make_unique<ExportActionFactory>(sigOS, bridgeOS);
    int i=0;

#ifdef _MSC_VER
    Tool.appendArgumentsAdjuster(
        getInsertArgumentAdjuster("-U_MSC_VER", ArgumentInsertPosition::BEGIN)
    );
#endif
    int result = Tool.run(factory.get());
    // Close streams (happens automatically on destruction, but explicit flush is good)
    sigOS.flush();
    bridgeOS.flush();

    return result;
}