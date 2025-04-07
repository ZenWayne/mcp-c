#include "clang/AST/ASTContext.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Lex/PreprocessorOptions.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/Error.h"
#include <fstream>
#include <sstream>
#include <memory>
#include <map>

using namespace clang;
using namespace clang::ast_matchers;
using namespace clang::tooling;
using namespace llvm;

static cl::OptionCategory MyToolCategory("MCPC Options");

// 添加两个输出文件选项
static cl::opt<std::string> SigOutputFilename(
    "s",
    cl::desc("指定函数签名输出文件名"),
    cl::value_desc("filename"),
    cl::cat(MyToolCategory));

static cl::opt<std::string> BridgeOutputFilename(
    "b",
    cl::desc("指定桥接代码输出文件名"),
    cl::value_desc("filename"),
    cl::cat(MyToolCategory));

// 结构体信息存储
struct StructInfo {
    std::vector<const clang::FieldDecl*> fields;
};
std::map<std::string, StructInfo> structs;

// 类型映射
const std::map<std::string, std::string> cjsontype_map = {
    {"int", "valueint"},
    {"bool", "valueint"},
    {"char *", "valuestring"},
    {"char*", "valuestring"}
};

// 结构体解析器
class StructParser : public MatchFinder::MatchCallback {
    raw_fd_ostream &OS;
    raw_fd_ostream &sigOS;
    std::vector<const FunctionDecl*> bridgeFunctions;
    ASTContext *Context;  // 添加 ASTContext 成员变量
    
    void generate_parser(const std::string &struct_name) {
        errs() << "生成解析函数: size=" << structs.size() << "\n";
        if (structs.find(struct_name) == structs.end()) return;
        
        OS << "inline " << struct_name << "* parse_" << struct_name 
           << "(cJSON *json) {\n"
           << "    " << struct_name << "* obj = (" << struct_name << "*)malloc(sizeof(" << struct_name << "));\n";
        
        for (const auto &FD : structs[struct_name].fields) {
            clang::QualType type = FD->getType();
            std::string type_str = type.getAsString();
            if (cjsontype_map.count(type_str)) {
                OS << "    obj->" << FD->getNameAsString() << " = cJSON_GetObjectItem(json, \""
                   << FD->getNameAsString() << "\")->" << cjsontype_map.at(type_str) << ";\n";
            } else if (type->isPointerType()) {
                const clang::PointerType *PT = type->getAs<clang::PointerType>();
                std::string pointeeType_str = PT->getPointeeType().getAsString();
                
                if (structs.count(pointeeType_str)) {
                    OS << "    obj->" << FD->getNameAsString() << " = parse_" 
                       << pointeeType_str
                       << "(cJSON_GetObjectItem(json, \"" << FD->getNameAsString() << "\"));\n";
                }
            }
        }
        OS << "    return obj;\n}\n\n";
    }

public:
    StructParser(raw_fd_ostream &OS_, raw_fd_ostream &sigOS_) : OS(OS_), sigOS(sigOS_), Context(nullptr) {
        OS << "// 桥接代码自动生成，请勿修改\n"
           << "#include \"cJSON.h\"\n"
           << "#include \"string.h\"\n"
           << "#include \"stdlib.h\"\n"
           << "#include \"function_signature.h\"\n";

    
        errs() << "函数签名生成器初始化\n";
        sigOS << "// 函数签名自动生成，请勿修改\n"
              << "#include \"string.h\"\n"
              << "#include \"function_signature.h\"\n\n"
              << "#include \"cJSON.h\"\n";

    }

    void generate_extern_declarations() {
        OS << "// 外部函数声明\n";
        for (const FunctionDecl *FD : bridgeFunctions) {
            std::string name = FD->getNameAsString();
            OS << "extern " << FD->getReturnType().getAsString() << " " << name << "(";
            for (unsigned i = 0; i < FD->getNumParams(); ++i) {
                if (i > 0) OS << ", ";
                const ParmVarDecl *param = FD->getParamDecl(i);
                OS << param->getType().getAsString() << " " << param->getNameAsString();
            }
            OS << ");\n";
        }
        OS << "\n";
    }

    void generate_signature() {
        // 生成函数签名结构体
        for (const FunctionDecl *FD : bridgeFunctions) {
            std::string name = FD->getNameAsString();
 
            if (const AnnotateAttr *attr = FD->getAttr<AnnotateAttr>()) {
                if (attr->getAnnotation().starts_with("EXPORT_AS")) {
                    name = attr->getAnnotation().substr(10).str();
                }
            }
            errs() << "导出函数: " << name << "\n";
            errs() << "函数返回值: " << FD->getReturnType().getAsString() << "\n";
            
            // 生成参数数组
            sigOS << "static struct ParameterInfo " << name << "_params[] = {\n";
            for (unsigned i = 0; i < FD->getNumParams(); ++i) {
                const ParmVarDecl *param = FD->getParamDecl(i);
                sigOS << "    {\"" << param->getType().getAsString() << "\", \""
                   << param->getNameAsString() << "\"}";
                if (i < FD->getNumParams() - 1) {
                    sigOS << ",";
                }
                sigOS << "\n";
            }
            sigOS << "};\n\n";

            // 生成函数签名结构体
            sigOS << "struct FunctionSignature " << name << "_signature = {\n";
            sigOS << "    \"" << FD->getReturnType().getAsString() << "\",\n";
            sigOS << "    " << FD->getNumParams() << ",\n";
            sigOS << "    " << name << "_params\n";
            sigOS << "};\n\n";
        }

        // 生成函数签名字典
        sigOS << "// 定义函数签名字典\n";
        sigOS << "static struct {\n";
        sigOS << "    const char* name;\n";
        sigOS << "    struct FunctionSignature* signature;\n";
        sigOS << "} function_signature_map[] = {\n";
        
        for (const FunctionDecl *FD : bridgeFunctions) {
            std::string name = FD->getNameAsString();
            if (const AnnotateAttr *attr = FD->getAttr<AnnotateAttr>()) {
                if (attr->getAnnotation().starts_with("EXPORT_AS")) {
                    name = attr->getAnnotation().substr(10).str();
                }
            }
            sigOS << "    {\"" << name << "\", &" << name << "_signature},\n";
        }
        sigOS << "    {NULL, NULL}  // 结束标记\n";
        sigOS << "};\n\n";

        // 生成获取所有函数签名的JSON函数
        sigOS << "cJSON* get_all_function_signatures_json() {\n";
        sigOS << "    cJSON* root = cJSON_CreateObject();\n";
        sigOS << "    if (root == NULL) {\n";
        sigOS << "        return NULL;\n";
        sigOS << "    }\n\n";
        
        sigOS << "    // 遍历所有函数签名\n";
        sigOS << "    for (int i = 0; function_signature_map[i].name != NULL; i++) {\n";
        sigOS << "        const char* func_name = function_signature_map[i].name;\n";
        sigOS << "        struct FunctionSignature* sig = function_signature_map[i].signature;\n\n";
        
        sigOS << "        cJSON* func_obj = cJSON_CreateObject();\n";
        sigOS << "        if (func_obj == NULL) {\n";
        sigOS << "            cJSON_Delete(root);\n";
        sigOS << "            return NULL;\n";
        sigOS << "        }\n\n";
        
        sigOS << "        // 添加返回类型\n";
        sigOS << "        cJSON_AddStringToObject(func_obj, \"return_type\", sig->returnType);\n";
        sigOS << "        \n";
        sigOS << "        // 添加参数数量\n";
        sigOS << "        cJSON_AddNumberToObject(func_obj, \"param_count\", sig->parameterCount);\n";
        sigOS << "        \n";
        sigOS << "        // 创建参数数组\n";
        sigOS << "        cJSON* params = cJSON_CreateArray();\n";
        sigOS << "        if (params == NULL) {\n";
        sigOS << "            cJSON_Delete(root);\n";
        sigOS << "            return NULL;\n";
        sigOS << "        }\n";
        sigOS << "        \n";
        sigOS << "        // 添加每个参数的信息\n";
        sigOS << "        for (int j = 0; j < sig->parameterCount; j++) {\n";
        sigOS << "            cJSON* param = cJSON_CreateObject();\n";
        sigOS << "            if (param == NULL) {\n";
        sigOS << "                cJSON_Delete(root);\n";
        sigOS << "                return NULL;\n";
        sigOS << "            }\n";
        sigOS << "            \n";
        sigOS << "            cJSON_AddStringToObject(param, \"type\", sig->params[j].type);\n";
        sigOS << "            cJSON_AddStringToObject(param, \"name\", sig->params[j].name);\n";
        sigOS << "            cJSON_AddItemToArray(params, param);\n";
        sigOS << "        }\n";
        sigOS << "        \n";
        sigOS << "        cJSON_AddItemToObject(func_obj, \"params\", params);\n";
        sigOS << "        cJSON_AddItemToObject(root, func_name, func_obj);\n";
        sigOS << "    }\n\n";
        sigOS << "    return root;\n";
        sigOS << "}\n";
    }

    ~StructParser() {
        OS << "\n#ifdef __cplusplus\n";
        OS << "}\n";
        OS << "#endif\n";
        OS.close();
        
        sigOS << "\n#ifdef __cplusplus\n";
        sigOS << "}\n";
        sigOS << "#endif\n";
        sigOS.close();
    }

    void generate_bridge_function() {
        OS << "cJSON* bridge(cJSON* input_json) {\n"
           << "    const char* func_name = cJSON_GetObjectItem(input_json, \"func\")->valuestring;\n"
           << "    cJSON* param_json = cJSON_GetObjectItem(input_json, \"param\");\n";

        errs() << "桥接函数数量: " << bridgeFunctions.size() << "\n";
        errs() << "结构体数量: " << structs.size() << "\n";
       // 生成所有桥接函数
        for (const FunctionDecl *FD : bridgeFunctions) {
            OS << "    if (strcmp(func_name, \"" << FD->getNameAsString() << "\") == 0) {\n";
            
            // 参数解析
            for (unsigned i = 0; i < FD->getNumParams(); ++i) {
                const ParmVarDecl *param = FD->getParamDecl(i);
                clang::QualType type = param->getType();
                
                errs()<<FD->getNameAsString()<< "函数入参参数类型: " << type << "\n";
                errs() << "类型指针: " << type.getTypePtr() << "\n";
                errs() << "是否指针类型: " << type.getTypePtr()->isPointerType() << "\n";
                if (type.getTypePtr()->isPointerType()) {
                    errs() << "入参类型: " << type.getAsString() << "\n";
                    const clang::PointerType* pointerType = type.getTypePtr()->getAs<clang::PointerType>();
                    clang::QualType pointeeType = pointerType->getPointeeType();
                    errs() << "入参类型: " << pointeeType.getAsString() << "\n";
                    OS << "        " << pointeeType.getAsString() << "* " << param->getNameAsString() << " = parse_" 
                       << pointeeType.getAsString() << "(cJSON_GetObjectItem(param_json, \""
                       << param->getNameAsString() << "\"));\n";
                } else {
                    std::string type_str = type.getAsString();
                    if (cjsontype_map.count(type_str)) {
                        type_str = cjsontype_map.at(type_str);
                    }else {
                        errs() << "不支持的类型: " << type_str << "\n";
                        continue;
                    }
                    OS << "        " << type << " " << param->getNameAsString() 
                       << " = cJSON_GetObjectItem(param_json, \"" << param->getNameAsString() 
                       << "\")->" << type_str << ";\n";
                }
            }
            
            // 函数调用和清理
            OS << "        cJSON* result = " << FD->getNameAsString() << "(";
            for (unsigned i = 0; i < FD->getNumParams(); ++i) {
                if (i > 0) OS << ", ";
                const ParmVarDecl *param = FD->getParamDecl(i);
                clang::QualType type = param->getType();
                if (type.getTypePtr()->isPointerType()) {
                    OS << param->getNameAsString();
                } else {
                    OS << param->getNameAsString();
                }
            }
            OS << ");\n";
            
            // 释放指针参数
            for (unsigned i = 0; i < FD->getNumParams(); ++i) {
                const ParmVarDecl *param = FD->getParamDecl(i);
                clang::QualType type = param->getType();
                if (type.getTypePtr()->isPointerType()) {
                    OS << "        free(" << param->getNameAsString() << ");\n";
                }
            }
            
            OS << "        return result;\n    }\n";
        }
        OS << "    return NULL;\n}\n";
    }
    void generate_header_file() {
        if (Context) {
            SourceManager &SM = Context->getSourceManager();
            FileID FID = SM.getMainFileID();
            if (const FileEntry *FE = SM.getFileEntryForID(FID)) {
                StringRef filename = FE->tryGetRealPathName();
                std::string header_filename = filename.str();
                size_t dot_pos = header_filename.rfind('.');
                if (dot_pos != std::string::npos) {
                    header_filename = header_filename.substr(0, dot_pos) + ".h";
                }
                size_t last_slash = header_filename.find_last_of("/\\");
                if (last_slash != std::string::npos) {
                    size_t second_last_slash = header_filename.find_last_of("/\\", last_slash - 1);
                    if (second_last_slash != std::string::npos) {
                        header_filename = header_filename.substr(second_last_slash + 1);
                    }
                }
                errs() << "开始处理翻译单元: " << header_filename << "\n";
                OS << "#include \"" << header_filename << "\"\n\n";
            }
        }
    }
    void onEndOfTranslationUnit() {

        generate_header_file();
        OS << "#ifdef __cplusplus\n"
           << "extern \"C\" {\n"
           << "#endif\n\n";
        generate_extern_declarations();
        // 生成所有结构体解析函数
        for (const auto &[name, _] : structs) {
            generate_parser(name);
        }
        
        generate_bridge_function();
        generate_signature();
    }

    void run(const MatchFinder::MatchResult &Result) override {
        Context = Result.Context;  // 设置 Context
        
        if (const RecordDecl *RD = Result.Nodes.getNodeAs<RecordDecl>("structDecl")) {
            StructInfo info;
            errs() << "解析结构体: " << RD->getNameAsString() << "\n";
            for (const FieldDecl *FD : RD->fields()) {
                info.fields.push_back(FD);
            }
            structs[RD->getNameAsString()] = info;
        } else if (const FunctionDecl *FD = Result.Nodes.getNodeAs<FunctionDecl>("exportedFunction")) {
            errs() << "解析接口函数: " << FD->getNameAsString() << "\n";
            bridgeFunctions.push_back(FD);
        }
    }
};

int main(int argc, const char **argv) {
    auto ExpParser = CommonOptionsParser::create(argc, argv, MyToolCategory);
    if (!ExpParser) {
        errs() << toString(ExpParser.takeError()) << "\n";
        return 1;
    }

    // 检查输出文件参数
    if (SigOutputFilename.empty() || BridgeOutputFilename.empty()) {
        errs() << "必须指定两个输出文件: -s 和 -b\n";
        return 1;
    }

    // 打开输出文件
    std::error_code EC;
    raw_fd_ostream sigOS(SigOutputFilename, EC, sys::fs::OF_Text);
    raw_fd_ostream bridgeOS(BridgeOutputFilename, EC, sys::fs::OF_Text);
    
    // 初始化匹配器
    MatchFinder finder;
    
    // 结构体解析
    StructParser structParser(bridgeOS, sigOS);
    finder.addMatcher(
        recordDecl(
            allOf(
                isStruct(),
                hasAttr(attr::Annotate)
            )
        ).bind("structDecl"),
        &structParser
    );
    finder.addMatcher(
        functionDecl(hasAttr(attr::Annotate)).bind("exportedFunction"),
        &structParser
    );
    
    // 使用自定义的 FrontendAction
    ClangTool tool(ExpParser->getCompilations(), ExpParser->getSourcePathList());
#ifdef _MSC_VER
    tool.appendArgumentsAdjuster(
        getInsertArgumentAdjuster("-U_MSC_VER", ArgumentInsertPosition::BEGIN)
    );
#endif
    int result = tool.run(newFrontendActionFactory(&finder).get());

    return result;
}
