# mcp-c
mcp server frarmwork written in c, developing efficiently and effortlessly
utilize annotate to generate code automatically, only need to focus on what to need to write mcp server function

## how to use
1. write your own mcp tools under `src/mcp_server`
   - add attributes `EXPORT` and `EXPORT_AS(name)` to the functions and structs you want to use
2. build the project, `cmake -B build -S .  && cmake --build .`, export.cpp will generate all the tedious code for you(like tools list, bridge json to struct, etc.)
3. run the project, `./mcpc`

## quick start
1. prerequisites
- vcpkg(only for windows)
- cmake
- ninja
- clang
- cJSON
2. build the project
windows
```bash
cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=ON ^
    -DCMAKE_TOOLCHAIN_FILE="path/to/vcpkg/scripts/buildsystems/vcpkg.cmake" ^
    -G "Ninja" ^
    -DVCPKG_APPLOCAL_DEPS=ON ^
    -DCMAKE_BUILD_TYPE=Release ^
    -B build -S . 

powershell.exe -Command "if (Test-Path 'build\compile_commands.json') { (Get-Content -Path 'build\compile_commands.json') -replace '/', '\\' | Set-Content -Path 'build\compile_commands.json' }"
```

1. run the project
```bash
./mcpc
```

## supported feature
1. export struct
first you need to add the macro to the struct
consider this file name struct.c
```c
#include "export_macro.h" //first include the export macro
typedef enum EXPORT COLOR {
    RED,
    GREEN,
    BLUE
} COLOR;

typedef struct EXPORT_AS(cloth) cloth {
    enum COLOR color DES("color of the cloth");
    int size;
} cloth;

typedef struct EXPORT person {
    bool isMale DES("whether is male or not male is true");
    int age DES("age of the person");
    char* name;
    cloth wearing_cloths;
} person;
```

after you execute export.exe struct.c
this will generate the serialize code under the src folder
generated_bridge_code.c contained the code to parse a json to object
generated_function_signatures.c contained the code of definition of the struct

the above code sample will generate a get_all_function_signatures_json function 
to generate definition json like the following
```json
{
        "$defs":        {
                "COLOR":        {
                        "type": "integer",
                        "enum": ["RED", "GREEN", "BLUE"]
                },
                "cloth":        {
                        "type": "object",
                        "properties":   {
                                "color":        {
                                        "$ref": "#/$defs/COLOR",
                                        "description":  "color of the cloth"
                                },
                                "size": {
                                        "type": "integer"
                                }
                        },
                        "required":     ["color", "size"]
                },
                "person":       {
                        "type": "object",
                        "properties":   {
                                "isMale":       {
                                        "type": "boolean",
                                        "description":  "whether is male or not male is true"
                                },
                                "age":  {
                                        "type": "integer",
                                        "description":  "age of the person"
                                },
                                "name": {
                                        "type": "string"
                                },
                                "wearing_cloths":       {
                                        "$ref": "#/$defs/cloth"
                                }
                        },
                        "required":     ["isMale", "age", "name", "wearing_cloths"]
                }
        },
        "tools":[]
}
```

2. export function signature and struct via annotate

first you need to add the macro for functions and struct you need to export
consider this file name func.c
```c
#include "export_macro.h" //first include the export macro
... //struct definition here, with the code sample in section 1
EXPORT_AS(get_color) //if the function is a handler of tool "get_color", you need to add this macro
cJSON* get_color(COLOR c)
{

}

EXPORT_AS(get_person_info) //if the function is a handler of tool "get_person_info", you need to add this macro
cJSON* get_person_info(person* p, int id)
{

}

EXPORT_AS(tools, list) // if the function is a handler of tool "tools/list", you need to add this macro, use the comma to separate the tool name and the function name
cJSON* tools_list() {
    int i = 0;
    return get_all_function_signatures_json();
}
```

this will generate a get_all_function_signatures_json function which will return the following function signature json
```json
{
   "#def": { ... } // param type defition here
   "tools":        [{
                        "name": "tools/list",
                        "description":  "",
                        "inputSchema":  {
                                "type": "object",
                                "properties":   {
                                },
                                "required":     [],
                                "additionalProperties": false,
                                "$schema":      "http://json-schema.org/draft-07/schema#"
                        }
                }, {
                        "name": "get_color",
                        "description":  "",
                        "inputSchema":  {
                                "type": "object",
                                "properties":   {
                                        "c":    {
                                                "$ref": "#/$defs/COLOR"
                                        }
                                },
                                "required":     ["c"],
                                "additionalProperties": false,
                                "$schema":      "http://json-schema.org/draft-07/schema#"
                        }
                }, {
                        "name": "get_person_info",
                        "description":  "",
                        "inputSchema":  {
                                "type": "object",
                                "properties":   {
                                        "p":    {
                                                "$ref": "#/$defs/person"
                                        },
                                        "id":   {
                                                "type": "integer"
                                        }
                                },
                                "required":     ["p", "id"],
                                "additionalProperties": false,
                                "$schema":      "http://json-schema.org/draft-07/schema#"
                        }
     }]
}
```

3. use EXPORT for short

