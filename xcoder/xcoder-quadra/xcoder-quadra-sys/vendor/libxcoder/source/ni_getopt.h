/*******************************************************************************
 *
 * Copyright (C) 2022 NETINT Technologies
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 ******************************************************************************/

/*!*****************************************************************************
 *  \file   ni_getopt.h
 *
 *  \brief  Implementation of getopt() and getopt_long() for Windows environment
 ******************************************************************************/

#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

#ifdef _WIN32
#ifdef XCODER_DLL
#ifdef LIB_EXPORTS
#define LIB_API_GETOPT __declspec(dllexport)
#else
#define LIB_API_GETOPT __declspec(dllimport)
#endif
#else
#define LIB_API_GETOPT
#endif
#elif __linux__ || __APPLE__
#define LIB_API_GETOPT
#endif

extern LIB_API_GETOPT int optind, opterr, optopt;
extern LIB_API_GETOPT char *optarg;

/* Describe the long-named options requested by the application.
   The LONG_OPTIONS argument to getopt_long or getopt_long_only is a vector
   of 'struct option' terminated by an element containing a name which is
   zero.

   The field 'has_arg' is:
   no_argument          (or 0) if the option does not take an argument,
   required_argument    (or 1) if the option requires an argument,
   optional_argument    (or 2) if the option takes an optional argument.

   If the field 'flag' is not NULL, it points to a variable that is set
   to the value given in the field 'val' when the option is found, but
   left unchanged if the option is not found.

   To have a long-named option do something other than set an 'int' to
   a compiled-in constant, such as set a value from 'optarg', set the
   option's 'flag' field to zero and its 'val' field to a nonzero
   value (the equivalent single-letter option character, if there is
   one).  For long options that have a zero 'flag' field, 'getopt'
   returns the contents of the 'val' field.  */

struct option
{
    const char *name;
    /* has_arg can't be an enum because some compilers complain about
	   type mismatches in all the code that assumes it is an int.  */
    int has_arg;
    int *flag;
    int val;
};

/* Names for the values of the 'has_arg' field of 'struct option'.  */

#define no_argument             0
#define required_argument       1
#define optional_argument       2

extern LIB_API_GETOPT int getopt(int argc, char *argv[], const char *optstring);
extern LIB_API_GETOPT int getopt_long(int argc, char* argv[], const char* optstring, const struct option *longopts, int *longindex);

#ifdef __cplusplus
}
#endif
