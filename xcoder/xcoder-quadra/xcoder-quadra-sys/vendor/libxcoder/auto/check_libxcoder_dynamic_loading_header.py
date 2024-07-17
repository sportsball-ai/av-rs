#!/usr/bin/env python3

import os
import sys
import re

default_dl_header = '../source/ni_libxcoder_dynamic_loading.h'
# libxcoder header files to check for in. These must be in same folder with
# ni_libxcoder_dynamic_loading.h
header_files = ['ni_av_codec.h', 'ni_util.h', 'ni_device_api.h']

# STEVENTODO: these capitalization replacements aren't used but somehow tests still pass. Investigate
function_name_to_api_function_name_keyword_replacements = {'420p': '420P',
                                                           '444p': '444P',
                                                           'P2p': 'P2P'}

func_proto_dict = {"return_type"    : "",
                   "function_name"  : "",
                   "input_args_list": []} # list elements are strings of arg type&name
                                          # (eg. "int width")

# ex. "ni_copy_yuv_444p_to_420p" to "PNICOPYYUV444PTO420P"
def function_name_to_api_function_pointer_name(s):
    return 'P' + s.replace('_', '').upper()

# ex. "ni_copy_yuv_444p_to_420p" to "niCopyYuv444PTo420P"
def function_name_to_api_function_name(s):
    assert s.startswith('ni_'), \
           "Found '{}' labeled with 'LIB_API' but missing 'ni_' prefix".format(s)
    result = ''
    p = ''
    for c in s:
        if c != '_':
            result += c.upper() if p == '_' else c
        p = c
    for key, value in function_name_to_api_function_name_keyword_replacements.items():
        result = result.replace(key, value)
    return result


# read a header file, return list of func_proto_dict
def extract_function_prototypes(header_file):
    def rstrip_comments(s):
        re.sub(r"//.*$", "", s)
        re.sub(r"/\*.*\*\/$","", s)
        s.rstrip()
        return s

    def func_proto_string_to_dict(fp_str):
        fp_dict = func_proto_dict.copy()

        # get return_type and function_name
        args_idx = fp_str.find('(')
        if args_idx == -1:
            raise RuntimeError("(FAILURE) Could not find first '(' for function prototype: {}"\
                               .format(fp_str))
        match_obj = re.search(r"LIB_API((\s+[^ *&]+)+)([ *&]+)([^ *&]+) *$", fp_str[:args_idx])
        if not match_obj:
            raise RuntimeError("(FAILURE) Could not determine return_type and function_name "
                               "for function prototype: {}".format(fp_str))
        fp_dict["return_type"] = match_obj.group(1).strip() if not match_obj.group(3).strip() else \
                                 match_obj.group(1).strip() + " " + match_obj.group(3).strip()
        rt_match_obj = re.search(r"(\s*)NI_DEPRECATED(\s*)", fp_dict["return_type"])
        if rt_match_obj and (rt_match_obj.group(1) or rt_match_obj.group(2)):
            fp_dict["return_type"] = re.sub(r"\s*NI_DEPRECATED\s*", " ", fp_dict["return_type"])
        fp_dict["return_type"] = fp_dict["return_type"].strip()
        fp_dict["function_name"] = match_obj.group(4).strip()

        # get input args
        args_list = fp_str[args_idx+1:-2].split(',') # assume fp_str ends with ');'
        args_list = [x.strip() for x in args_list]
        if len(args_list) == 1 and not args_list[0]:
            # there are no input args for this function
            # note: "func_name(void)" is special in C and will thus be put in "input_args_list"
            #       https://stackoverflow.com/a/5929736
            fp_dict["input_args_list"] = []
        else:
            fp_dict["input_args_list"] = args_list
            # Unused code to separate args into datatype and name
            # for arg_str in args_list:
                # match_obj = re.search(r"([^ *&]+)([ *&]+)([^ *&]+)$", arg_str.strip())
                # if not match_obj:
                    # raise RuntimeError("(FAILURE) Could not determine datatype and name for "
                                       # "input arg '{}' in function prototype: {}"\
                                       # .format(arg_str, fp_str))
                # datatype = match_obj.group(1) + match_obj.group(2).strip()
                # arg_name = match_obj.group(3)
                # fp_dict["input_args_list"].append((datatype, arg_name))
        return fp_dict

    # parse function text
    fp_dict_list = []
    with open(header_file) as h:
        lines = h.readlines()
    index = 0
    while index < len(lines):
        line = rstrip_comments(lines[index].strip())
        if re.search(r"^LIB_API\s", line):
            s = line
            while not line.endswith(');'):
                index += 1
                line = rstrip_comments(lines[index].strip())
                s = s + " " + line
            fp_dict_list.append(func_proto_string_to_dict(s))
        index += 1
    return fp_dict_list

# generate function pointer line
# ex. "typedef void (LIB_API* PNICOPYYUV444PTO420P) (uint8_t *p_dst0[NI_MAX_NUM_DATA_POINTERS], uint8_t *p_dst1[NI_MAX_NUM_DATA_POINTERS], uint8_t *p_src[NI_MAX_NUM_DATA_POINTERS], int width, int height, int factor, int mode);"
def gen_func_typedef_line(fp_dict):
    return "typedef " + fp_dict["return_type"] + " (LIB_API* " + \
           function_name_to_api_function_pointer_name(fp_dict["function_name"]) + ") (" + \
           ", ".join(fp_dict["input_args_list"]) + ");\n"

# generate function list line (NOTE: actual line will have spacing to simulate columns between
# datatype, name, and comments)
# ex. "PNICOPYYUV444PTO420P                 niCopyYuv444PTo420P;                  /** Client should access ::ni_copy_yuv_444p_to_420p API through this pointer */"
def gen_func_struct_member_line(fp_dict):
    col1 = "    " + function_name_to_api_function_pointer_name(fp_dict["function_name"])
    col1 = "{0: <41}".format(col1)
    col2 = function_name_to_api_function_name(fp_dict["function_name"]) + ";"
    col2 = "{0: <38}".format(col2)
    col3 = "/** Client should access ::" + fp_dict["function_name"] + " API through this pointer */\n"
    return col1 + col2 + col3

# generate APICreateInstance class line
# ex. "functionList->niCopyYuv444PTo420P = reinterpret_cast<decltype(ni_copy_yuv_444p_to_420p)*>(dlsym(lib,"ni_copy_yuv_444p_to_420p"));"
def gen_func_struct_init_line(fp_dict):
    return "        functionList->" + function_name_to_api_function_name(\
           fp_dict["function_name"]) + " = reinterpret_cast<decltype(" + \
           fp_dict["function_name"] + ')*>(dlsym(lib,"' + fp_dict["function_name"] + '"));\n'

def main(dl_header=default_dl_header):
    rc = 0
    fp_dict_list = []
    api_header_paths = [os.path.dirname(dl_header) + "/" + x for x in header_files]

    print("Checking dynamic loading header file: " + dl_header)
    print("Checking function prototypes from headers: " + " ".join(api_header_paths))

    # check all files to be read exist
    for filepath in [dl_header] + api_header_paths:
        if not os.path.isfile(filepath):
            print("(FAILURE) file does not exist: " + filepath)
            return 1

    for header_file in header_files:
        fp_dict_list.extend(extract_function_prototypes('{}/{}'\
                            .format(os.path.dirname(dl_header), header_file)))

    # Check for duplicate function names
    func_names_list = [x["function_name"] for x in fp_dict_list]
    seen_set = set()
    dup_func_names = [x for x in func_names_list if x in seen_set or seen_set.add(x)]
    if dup_func_names:
        rc = 1
        print('(FAILURE) duplicated function names in libxcoder API: ' + ', '.join(dup_func_names))
    del func_names_list, seen_set, dup_func_names
    if rc: return rc

    # Check all functions present in dynamic loading header
    with open(dl_header) as h:
        dl_header_lines = h.readlines()
    for fp_dict in fp_dict_list:
        try:
            dl_header_lines.remove(gen_func_typedef_line(fp_dict))
        except ValueError:
            print("(ERROR) could not find: " + gen_func_typedef_line(fp_dict))
            rc |= 1
    for fp_dict in fp_dict_list:
        try:
            dl_header_lines.remove(gen_func_struct_member_line(fp_dict))
        except ValueError:
            print("(ERROR) could not find: " + gen_func_struct_member_line(fp_dict))
            rc |= 1
    for fp_dict in fp_dict_list:
        try:
            dl_header_lines.remove(gen_func_struct_init_line(fp_dict))
        except ValueError:
            print("(ERROR) could not find: " + gen_func_struct_init_line(fp_dict))
            rc |= 1
    if rc: return rc

    # Check there are no duplicate or errant references to functions in dynamic loading header
    for line in dl_header_lines:
        if any(x in line for x in ["typedef", "Client should", "functionList->"]):
            if "typedef struct _NETINT_LIBXCODER_API_FUNCTION_LIST" in line:
                continue
            print("(ERROR) unexpected line in " + dl_header + ": " + line)
            print("Perhaps there is a duplicate function in dynamic loading header file; "
                  "or, the function is not labeled with 'LIB_API' in header file")
            rc |= 1

    if not rc: print('(SUCCESS)')
    return rc


if __name__ == '__main__':
    if len(sys.argv) > 1:
        rc = main(sys.argv[1])
    else:
        rc = main()
    exit(rc)