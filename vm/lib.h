#ifndef _CLIPCC_LIB_H_
#define _CLIPCC_LIB_H_

#include <string>

#include "variant.h"

namespace clipcc {

int random(int a, int b);
double random(double a, double b);
variant random(variant a, variant b);

double rad(double x);
double deg(double x);

bool is_int(double x);
std::string join(const std::string &a, const std::string &b);

}

#endif // !_CLIPCC_LIB_H_
