// Minimalna atrapa srodowiska Arduino/firmware — do uruchomienia kodu "glue" z v3.ino na PC.
#pragma once
#include <string>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>
#include <algorithm>
#include <stdint.h>
#include <climits>
#include <cstddef>
static uint32_t millis(){return 100000;}
class String {
 public:
  std::string s;
  String(){} String(const char*c):s(c?c:""){} String(const std::string&x):s(x){}
  String(int v):s(std::to_string(v)){} String(long v):s(std::to_string(v)){} String(unsigned v):s(std::to_string(v)){}
  const char*c_str()const{return s.c_str();} size_t length()const{return s.size();}
  int indexOf(char c)const{auto p=s.find(c);return p==std::string::npos?-1:(int)p;}
  int lastIndexOf(char c)const{auto p=s.rfind(c);return p==std::string::npos?-1:(int)p;}
  int lastIndexOf(const char*c)const{auto p=s.rfind(c);return p==std::string::npos?-1:(int)p;}
  String substring(int a)const{return String(s.substr(a));}
  String substring(int a,int b)const{return String(s.substr(a,b-a));}
  bool startsWith(const char*p)const{return s.rfind(p,0)==0;}
  void trim(){size_t a=s.find_first_not_of(" \t\r\n");size_t b=s.find_last_not_of(" \t\r\n");s=(a==std::string::npos)?"":s.substr(a,b-a+1);}
  void toCharArray(char*d,unsigned n)const{strlcpy(d,s.c_str(),n);}
  String&operator+=(const String&o){s+=o.s;return *this;}
  bool operator!=(const String&o)const{return s!=o.s;} bool operator==(const String&o)const{return s==o.s;}
  char operator[](int i)const{return s[i];}
};
inline String operator+(const String&a,const String&b){return String(a.s+b.s);}
inline String operator+(const String&a,const char*b){return String(a.s+b);}
inline String operator+(const char*a,const String&b){return String(std::string(a)+b.s);}
inline String operator+(const String&a,long b){return String(a.s+std::to_string(b));}
