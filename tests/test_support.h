#pragma once
#include <stdexcept>
#include <cmath>
#include <iostream>
#define CHECK(expression) do{if(!(expression))throw std::runtime_error(std::string(__FILE__)+":"+std::to_string(__LINE__)+": " #expression);}while(false)
inline bool Near(float a,float b,float epsilon=.0001f){return std::abs(a-b)<=epsilon;}
template<class F>bool Throws(F f){try{f();return false;}catch(const std::exception&){return true;}}
