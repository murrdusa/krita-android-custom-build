// SPDX-License-Identifier: GPL-2.0-or-later
#include "mobile_core/fill_tolerance_drag.h"

int main()
{
    if(mobileFillTolerance(30,0)!=30 ||
       mobileFillTolerance(30,6)!=30 ||
       mobileFillTolerance(30,-8)!=30)return 1;
    if(mobileFillTolerance(30,36)!=40 ||
       mobileFillTolerance(30,-36)!=20)return 2;
    if(mobileFillTolerance(70,1000)!=100 ||
       mobileFillTolerance(15,-1000)!=1)return 3;
}
