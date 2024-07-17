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
 *  \file   ni_getopt.c
 *
 *  \brief  Implementation of getopt() and getopt_long() for Windows environment
 ******************************************************************************/

#include <stdio.h>
#include <string.h>

#include "ni_getopt.h"

char    *optarg = NULL; // global argument pointer
int     optind = 0;     // global argv index
int     opterr = 1;     // global erroneous switch
int     optopt = 0;     // global erroneous option character

int getopt(int argc, char *argv[], const char *optstring)
{
    static char *nextchar = NULL;

    if (nextchar == NULL || *nextchar == '\0')
    {
        if (optind == 0)
            optind++;

        if (optind >= argc || argv[optind][0] != '-' || argv[optind][1] == '\0')
        {
            optarg = NULL;
            if (optind < argc)
                optarg = argv[optind];
            return EOF;
        }

        if (strncmp(argv[optind], "--", strlen("--")) == 0)
        {
            optind++;
            optarg = NULL;
            if (optind < argc)
                optarg = argv[optind];
            return EOF;
        }

        nextchar = argv[optind];
        nextchar += strlen("-");
        optind++;
    }

    char c = *nextchar++;
    char *cp = strchr(optstring, c);
    optopt = (int)c;
    if (cp == NULL || c == ':')
    {
        return '?';
    }

    cp++;
    if (*cp == ':')
    {
        if (*nextchar != '\0')
        {
            optarg = nextchar;
            nextchar = NULL;  // for next invocation
        }
        else if (optind < argc)
        {
            optarg = argv[optind];
            optind++;
        }
        else
        {
            return '?';
        }
    }

    return c;
}

int getopt_long(int argc, char* argv[], const char* optstring,
                const struct option* longopts, int* longindex)
{
    int i, parse_long_mismatch = 0;
    static char* nextchar = NULL;

    if (nextchar == NULL || *nextchar == '\0')
    {
        if (optind == 0)
        {
            optind++;
        }

        if (optind >= argc || argv[optind][0] != '-' || argv[optind][1] == '\0')
        {
            optarg = NULL;
            if (optind < argc)
            {
                optarg = argv[optind];
            }
            return EOF;
        }

        nextchar = argv[optind];
        if (strncmp(argv[optind], "--", 2) == 0)
        {
            parse_long_mismatch = 1;
            nextchar += strlen("--");
            optarg = NULL;
            if (optind < argc)
            {
                optarg = argv[optind];
            }
        }
        else
        {
            nextchar += strlen("-");
        }

        optind++;
    }

    // Parse long option string
    for (i = 0; longopts != NULL && longopts[i].name != NULL; i++)
    {
        size_t optlen = strlen(longopts[i].name);
        if (strncmp(nextchar, longopts[i].name, optlen) == 0)
        {
            nextchar += optlen;
            switch (longopts[i].has_arg)
            {
            case 0:
                if (*nextchar != '\0' || (optind < argc && argv[optind][0] != '-'))
                {
                    optind++;
                    return '?';
                }
                else
                {
                    optind++;
                    return longopts[i].flag == NULL ? longopts[i].val : 0;
                }
            case 1:
                if (*nextchar == '=')
                {
                    optarg = nextchar + 1;
                    optind++;
                    return longopts[i].flag == NULL ? longopts[i].val : 0;
                }
                else if (*nextchar != '\0' || (optind < argc && argv[optind][0] == '-'))
                {
                    optind++;
                    return '?';
                }
                else if (optind < argc)
                {
                    optarg = argv[optind];
                    optind++;
                    return longopts[i].flag == NULL ? longopts[i].val : 0;
                }
                else
                {
                    return '?';
                }
            case 2:
                if (*nextchar == '=')
                {
                    optarg = nextchar + 1;
                }
                else if (*nextchar == '\0' || (optind < argc && argv[optind][0] == '-'))
                {
                    optarg = NULL;
                }
                else
                {
                    if (*nextchar == '\0' && optind < argc)
                    {
                        optarg = argv[optind];
                    }
                }
                optind++;
                return longopts[i].flag == NULL ? longopts[i].val : 0;
            default:
                return '?';
            }
        }
    }

    if (parse_long_mismatch)
    {
        return '?';
    }

    // Parse short option string
    char c = *nextchar++;
    char* cp = strchr(optstring, c);
    optopt = (int)c;
    if (cp == NULL || c == ':')
    {
        return '?';
    }

    cp++;
    // Whether there is any argument
    if (*cp == ':')
    {
        if (*nextchar != '\0')
        {
            optarg = nextchar;
            nextchar = NULL;  // for next invocation
        }
        else if (optind < argc)
        {
            optarg = argv[optind];
            optind++;
        }
        else
        {
            return '?';
        }
    }

    return c;
}
