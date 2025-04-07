# mcp-c
mcp server frarmwork written in c, developing efficiently and effortlessly

## how to use
1. write your own mcp tools under `src/mcp_server`
   - add attributes `EXPORT` and `EXPORT_AS(name)` to the functions and structs you want to use
2. build the project, `cmake -B build -S .  && cmake --build .`, export.cpp will generate all the tedious code for you(like tools list, bridge json to struct, etc.)
3. run the project, `./mcpc`

