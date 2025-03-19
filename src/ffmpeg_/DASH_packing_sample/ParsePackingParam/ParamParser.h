#include <stdint.h>

#include "PackingParamsDef.h"

#ifndef _PARAMPARSER_H_
#define _PARAMPARSER_H_

class ParamParser
{
public:
    ParamParser() {};

    ~ParamParser() {};

    int32_t Parse(char *config_file, void *param_pointer);
private:
};

#endif /* _PARAMPARSER_H_ */
