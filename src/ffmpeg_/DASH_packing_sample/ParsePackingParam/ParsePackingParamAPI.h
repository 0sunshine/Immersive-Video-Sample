#ifndef _PARSEPACKINGPARAM_H_
#define _PARSEPACKINGPARAM_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void* Handler;

Handler ParsePackingParamInit();

int32_t ParsePackingParamParse(Handler hdl, char *config_file, void *param_pointer);

int32_t ParsePackingParamClose(Handler hdl);

#ifdef __cplusplus
}
#endif

#endif /* _PARSEPACKINGPARAM_H_ */
