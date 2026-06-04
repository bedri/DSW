#ifndef DSW_SCRIPT_MESCAL_H
#define DSW_SCRIPT_MESCAL_H

#include "script/script.h"
#include <string>
#include <univalue.h>

class CMescal
{
public:
    static CScript Compile(const std::string& jsonStr, std::string& errorStr);
    static UniValue Decompile(const CScript& script, std::string& errorStr);
};

#endif // DSW_SCRIPT_MESCAL_H
