// 函数签名 JSON 生成代码 (自动生成，请勿修改)
#include "cJSON.h"
#include "string.h" // For strcmp in potential enum parsing fallback

#include "file.h"

#ifdef __cplusplus
extern "C" {
#endif

cJSON* get_all_function_signatures_json() {
    cJSON* root = cJSON_CreateObject();
    if (!root) return NULL;

    // --- $defs --- 
    cJSON* defs = cJSON_CreateObject();
    if (!defs) { cJSON_Delete(root); return NULL; }
    cJSON_AddItemToObject(root, "$defs", defs);

    // Definition for enum: COLOR
    {
        cJSON* enum_def = cJSON_CreateObject();
        if (enum_def) {
            cJSON_AddStringToObject(enum_def, "type", "integer"); // Assuming underlying type maps well
            cJSON* enum_values = cJSON_CreateArray();
            if (enum_values) {
                cJSON_AddItemToArray(enum_values, cJSON_CreateString("RED"));
                cJSON_AddItemToArray(enum_values, cJSON_CreateString("GREEN"));
                cJSON_AddItemToArray(enum_values, cJSON_CreateString("BLUE"));
                cJSON_AddItemToObject(enum_def, "enum", enum_values);
            }
            cJSON_AddItemToObject(defs, "COLOR", enum_def);
        } // end if enum_def
    }

    // Definition for struct: cloth
    {
        cJSON* struct_def = cJSON_CreateObject();
        if (struct_def) {
            cJSON_AddStringToObject(struct_def, "type", "object");
            cJSON* properties = cJSON_CreateObject();
            if (properties) {
                cJSON* required_props = cJSON_CreateArray(); // Structs usually have all properties required unless optional pointers
                if(required_props) {
                // Field: color
                cJSON* cloth_color_schema = cJSON_CreateObject();
                if (cloth_color_schema) {
                    cJSON_AddStringToObject(cloth_color_schema, "$ref", "#/$defs/COLOR");
                    cJSON_AddStringToObject(cloth_color_schema, "description", "color of the cloth");
                    cJSON_AddItemToObject(properties, "color", cloth_color_schema);
                    cJSON_AddItemToArray(required_props, cJSON_CreateString("color"));
                }
                // Field: size
                cJSON* cloth_size_schema = cJSON_CreateObject();
                if (cloth_size_schema) {
                    cJSON_AddStringToObject(cloth_size_schema, "type", "integer");
                    cJSON_AddItemToObject(properties, "size", cloth_size_schema);
                    cJSON_AddItemToArray(required_props, cJSON_CreateString("size"));
                }
                    cJSON_AddItemToObject(struct_def, "properties", properties);
                    cJSON_AddItemToObject(struct_def, "required", required_props);
                 } else { cJSON_Delete(properties); } // end if required_props
            } // end if properties
            cJSON_AddItemToObject(defs, "cloth", struct_def);
        } // end if struct_def
    }

    // Definition for struct: person
    {
        cJSON* struct_def = cJSON_CreateObject();
        if (struct_def) {
            cJSON_AddStringToObject(struct_def, "type", "object");
            cJSON* properties = cJSON_CreateObject();
            if (properties) {
                cJSON* required_props = cJSON_CreateArray(); // Structs usually have all properties required unless optional pointers
                if(required_props) {
                // Field: isMale
                cJSON* person_isMale_schema = cJSON_CreateObject();
                if (person_isMale_schema) {
                    cJSON_AddStringToObject(person_isMale_schema, "type", "boolean");
                    cJSON_AddStringToObject(person_isMale_schema, "description", "whether is male or not male is true");
                    cJSON_AddItemToObject(properties, "isMale", person_isMale_schema);
                    cJSON_AddItemToArray(required_props, cJSON_CreateString("isMale"));
                }
                // Field: age
                cJSON* person_age_schema = cJSON_CreateObject();
                if (person_age_schema) {
                    cJSON_AddStringToObject(person_age_schema, "type", "integer");
                    cJSON_AddStringToObject(person_age_schema, "description", "age of the person");
                    cJSON_AddItemToObject(properties, "age", person_age_schema);
                    cJSON_AddItemToArray(required_props, cJSON_CreateString("age"));
                }
                // Field: name
                cJSON* person_name_schema = cJSON_CreateObject();
                if (person_name_schema) {
                    cJSON_AddStringToObject(person_name_schema, "type", "string");
                    cJSON_AddItemToObject(properties, "name", person_name_schema);
                    cJSON_AddItemToArray(required_props, cJSON_CreateString("name"));
                }
                // Field: wearing_cloths
                cJSON* person_wearing_cloths_schema = cJSON_CreateObject();
                if (person_wearing_cloths_schema) {
                    cJSON_AddStringToObject(person_wearing_cloths_schema, "$ref", "#/$defs/cloth");
                    cJSON_AddItemToObject(properties, "wearing_cloths", person_wearing_cloths_schema);
                    cJSON_AddItemToArray(required_props, cJSON_CreateString("wearing_cloths"));
                }
                    cJSON_AddItemToObject(struct_def, "properties", properties);
                    cJSON_AddItemToObject(struct_def, "required", required_props);
                 } else { cJSON_Delete(properties); } // end if required_props
            } // end if properties
            cJSON_AddItemToObject(defs, "person", struct_def);
        } // end if struct_def
    }

    // --- tools --- 
    cJSON* tools = cJSON_CreateArray();
    if (!tools) { cJSON_Delete(root); return NULL; }
    cJSON_AddItemToObject(root, "tools", tools);

    // Tool for function: tools/list
    {
        cJSON* tool = cJSON_CreateObject();
        if (tool) {
            cJSON_AddStringToObject(tool, "name", "tools/list");
            cJSON_AddStringToObject(tool, "description", ""); // Add empty description if none provided
            cJSON* inputSchema = cJSON_CreateObject();
            if (inputSchema) {
                cJSON_AddStringToObject(inputSchema, "type", "object");
                cJSON* properties = cJSON_CreateObject();
                cJSON* required = cJSON_CreateArray();
                if (properties && required) {
                    cJSON_AddItemToObject(inputSchema, "properties", properties);
                    cJSON_AddItemToObject(inputSchema, "required", required);
                } else { 
                    if (properties) cJSON_Delete(properties);
                    if (required) cJSON_Delete(required);
                    cJSON_Delete(inputSchema);
                    inputSchema = NULL;
                }
                 if (inputSchema) {
                     cJSON_AddFalseToObject(inputSchema, "additionalProperties");
                     cJSON_AddStringToObject(inputSchema, "$schema", "http://json-schema.org/draft-07/schema#");
                     cJSON_AddItemToObject(tool, "inputSchema", inputSchema);
                 }
            } // end if inputSchema
            if (inputSchema) { // Only add tool if input schema was successful
               cJSON_AddItemToArray(tools, tool);
            } else { cJSON_Delete(tool); } // Clean up tool if schema failed
        } // end if tool
    }

    // Tool for function: get_color
    {
        cJSON* tool = cJSON_CreateObject();
        if (tool) {
            cJSON_AddStringToObject(tool, "name", "get_color");
            cJSON_AddStringToObject(tool, "description", ""); // Add empty description if none provided
            cJSON* inputSchema = cJSON_CreateObject();
            if (inputSchema) {
                cJSON_AddStringToObject(inputSchema, "type", "object");
                cJSON* properties = cJSON_CreateObject();
                cJSON* required = cJSON_CreateArray();
                if (properties && required) {
                // Parameter: c
                cJSON* get_color_c_schema = cJSON_CreateObject();
                if (get_color_c_schema) {
                    cJSON_AddStringToObject(get_color_c_schema, "$ref", "#/$defs/COLOR");
                    cJSON_AddItemToObject(properties, "c", get_color_c_schema);
                    cJSON_AddItemToArray(required, cJSON_CreateString("c"));
                 }
                    cJSON_AddItemToObject(inputSchema, "properties", properties);
                    cJSON_AddItemToObject(inputSchema, "required", required);
                } else { 
                    if (properties) cJSON_Delete(properties);
                    if (required) cJSON_Delete(required);
                    cJSON_Delete(inputSchema);
                    inputSchema = NULL;
                }
                 if (inputSchema) {
                     cJSON_AddFalseToObject(inputSchema, "additionalProperties");
                     cJSON_AddStringToObject(inputSchema, "$schema", "http://json-schema.org/draft-07/schema#");
                     cJSON_AddItemToObject(tool, "inputSchema", inputSchema);
                 }
            } // end if inputSchema
            if (inputSchema) { // Only add tool if input schema was successful
               cJSON_AddItemToArray(tools, tool);
            } else { cJSON_Delete(tool); } // Clean up tool if schema failed
        } // end if tool
    }

    // Tool for function: get_person_info
    {
        cJSON* tool = cJSON_CreateObject();
        if (tool) {
            cJSON_AddStringToObject(tool, "name", "get_person_info");
            cJSON_AddStringToObject(tool, "description", ""); // Add empty description if none provided
            cJSON* inputSchema = cJSON_CreateObject();
            if (inputSchema) {
                cJSON_AddStringToObject(inputSchema, "type", "object");
                cJSON* properties = cJSON_CreateObject();
                cJSON* required = cJSON_CreateArray();
                if (properties && required) {
                // Parameter: p
                cJSON* get_person_info_p_schema = cJSON_CreateObject();
                if (get_person_info_p_schema) {
                    cJSON_AddStringToObject(get_person_info_p_schema, "$ref", "#/$defs/person");
                    cJSON_AddItemToObject(properties, "p", get_person_info_p_schema);
                    cJSON_AddItemToArray(required, cJSON_CreateString("p"));
                 }
                // Parameter: id
                cJSON* get_person_info_id_schema = cJSON_CreateObject();
                if (get_person_info_id_schema) {
                    cJSON_AddStringToObject(get_person_info_id_schema, "type", "integer");
                    cJSON_AddItemToObject(properties, "id", get_person_info_id_schema);
                    cJSON_AddItemToArray(required, cJSON_CreateString("id"));
                 }
                    cJSON_AddItemToObject(inputSchema, "properties", properties);
                    cJSON_AddItemToObject(inputSchema, "required", required);
                } else { 
                    if (properties) cJSON_Delete(properties);
                    if (required) cJSON_Delete(required);
                    cJSON_Delete(inputSchema);
                    inputSchema = NULL;
                }
                 if (inputSchema) {
                     cJSON_AddFalseToObject(inputSchema, "additionalProperties");
                     cJSON_AddStringToObject(inputSchema, "$schema", "http://json-schema.org/draft-07/schema#");
                     cJSON_AddItemToObject(tool, "inputSchema", inputSchema);
                 }
            } // end if inputSchema
            if (inputSchema) { // Only add tool if input schema was successful
               cJSON_AddItemToArray(tools, tool);
            } else { cJSON_Delete(tool); } // Clean up tool if schema failed
        } // end if tool
    }

    return root;
}

#ifdef __cplusplus
} // extern "C"
#endif
