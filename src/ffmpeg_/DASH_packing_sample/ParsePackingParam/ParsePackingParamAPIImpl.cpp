#include "ParsePackingParamAPI.h"
#include "ParamParser.h"

Handler ParsePackingParamInit()
{
    ParamParser *parser = new ParamParser();
    if (!parser)
        return NULL;

    return (Handler)((long)parser);
}

int32_t ParsePackingParamParse(Handler hdl, char *config_file, void *param_pointer)
{
    ParamParser *parser = (ParamParser *)hdl;
    if (!parser)
        return -1;

    int32_t ret = parser->Parse(config_file, param_pointer);

    return ret;
}

int32_t ParsePackingParamClose(Handler hdl)
{
    ParamParser *parser = (ParamParser *)hdl;
    if (parser)
    {
        delete parser;
        parser = NULL;
    }

    return 0;
}
