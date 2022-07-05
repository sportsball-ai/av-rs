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
* \file   ni_getopt.c
*
* \brief  Implementation of getopt and getopt_long with Windows API.
*
*******************************************************************************/

#ifdef _WIN32

#include <stdio.h>
#include "ni_getopt.h"

TCHAR *optarg = NULL;   // global argument pointer
int optind = 0;         // global argv index

int getopt(int argc, TCHAR *argv[], const TCHAR *optstring)
{
    static TCHAR *nextchar = NULL;

    if (nextchar == NULL || nextchar == _T('\0'))
    {
        if (optind == 0)
            optind++;

        if (optind >= argc || argv[optind][0] != _T('-') ||
            argv[optind][1] == _T('\0'))
        {
            optarg = NULL;
            if (optind < argc)
                optarg = argv[optind];
            return EOF;
        }

        if (_tcsncmp(argv[optind], _T("--"), _tcslen(_T("--"))) == 0)
        {
            optind++;
            optarg = NULL;
            if (optind < argc)
                optarg = argv[optind];
            return EOF;
        }

        nextchar = argv[optind];
        nextchar += _tcslen(_T("-"));
        optind++;
    }

    TCHAR c = *nextchar++;
    TCHAR *cp = _tcschr(optstring, c);

    if (cp == NULL || c == _T(':'))
        return _T('?');

    cp++;
    if (*cp == _T(':'))
    {
        if (*nextchar != _T('\0'))
        {
            optarg = nextchar;
            nextchar = NULL;   // for next invocation
        } else if (optind < argc)
        {
            optarg = argv[optind];
            optind++;
        } else
        {
            return _T('?');
        }
    }

    return c;
}

int getopt_long(int argc, TCHAR *argv[], const TCHAR *optstring,
                const struct option *longopts, int *longindex)
{
    int i;
    static TCHAR *nextchar = NULL;

    if (nextchar == NULL || *nextchar == _T('\0'))
    {
        if (optind == 0)
        {
            optind++;
        }

        if (optind >= argc || argv[optind][0] != _T('-') ||
            argv[optind][1] == _T('\0'))
        {
            optarg = NULL;
            if (optind < argc)
                optarg = argv[optind];
            return EOF;
        }

        nextchar = argv[optind];
        if (_tcsncmp(argv[optind], _T("--"), 2) == 0)
        {
            nextchar += _tcslen(_T("--"));
            optarg = NULL;
            if (optind < argc)
                optarg = argv[optind];
        } else
        {
            nextchar += _tcslen(_T("-"));
        }

        optind++;
    }

    // Parse long option string
    for (i = 0; longopts != NULL && longopts[i].name != NULL; i++)
    {
        size_t optlen = _tcslen(_T(longopts[i].name));
        if (_tcsncmp(nextchar, _T(longopts[i].name), optlen) == 0)
        {
            optarg = nextchar + optlen;
            switch (longopts[i].has_arg)
            {
                case 0:
                    if (*optarg != _T('\0') || argv[optind][0] != _T('-'))
                    {
                        return _T('?');
                    } else
                    {
                        return longopts[i].flag == NULL ? longopts[i].val : 0;
                    }
                case 1:
                    if (*optarg == _T('='))
                    {
                        optarg += _tcslen(_T("="));
                        return longopts[i].flag == NULL ? longopts[i].val : 0;
                    } else if (*optarg != _T('\0') ||
                               argv[optind][0] == _T('-'))
                    {
                        return _T('?');
                    } else
                    {
                        optarg = argv[optind];
                        return longopts[i].flag == NULL ? longopts[i].val : 0;
                    }
                case 2:
                    if (*optarg == _T('\0') || argv[optind][0] == _T('-'))
                    {
                        optarg = NULL;
                    } else
                    {
                        if (*optarg == _T('\0'))
                        {
                            optarg = argv[optind];
                        }
                    }
                    return longopts[i].flag == NULL ? longopts[i].val : 0;
                default:
                    return _T('?');
            }
        }
    }

    // Parse short option string
    TCHAR c = *nextchar++;
    TCHAR *cp = _tcschr(optstring, c);

    if (cp == NULL || c == _T(':'))
    {
        return _T('?');
    }

    cp++;
    // Whether there is any argument
    if (*cp == _T(':'))
    {
        if (*nextchar != _T('\0'))
        {
            optarg = nextchar;
            nextchar = NULL;   // for next invocation
        } else if (optind < argc)
        {
            optarg = argv[optind];
            optind++;
        } else
        {
            return _T('?');
        }
    }

    return c;
}
#endif
