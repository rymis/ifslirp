#ifndef OPTS_H__INCL
#define OPTS_H__INCL

#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OPT_PARSE_START(argc, argv, usage) \
    do { \
        int _opt_parse_index; \
        int _opt_parse_print_help = 0; \
        int _opt_parse_exit_code = 0; \
        for (_opt_parse_index = 1; _opt_parse_index < (argc);) { \
            const char* _opt_parse_option = (argv)[_opt_parse_index]; \
            const char* _opt_parse_argument = _opt_parse_index + 1 < (argc)? argv[_opt_parse_index + 1] : NULL; \
            int _opt_parse_has_argument = 0; \
            int _opt_parse_parsed = 0; \
            if (_opt_parse_print_help) { \
                fprintf(stderr, "Usage: %s %s\n", (argv)[0], usage); \
                fprintf(stderr, "-h, --help\n    print this message and exit\n"); \
            } else { \
                if (!strcmp(_opt_parse_option, "-h") || !strcmp(_opt_parse_option, "--help")) { \
                    _opt_parse_print_help = 1; \
                    continue; \
                } \
            } \
            do { \
            } while (0)

#define OPT_PARSE_END() \
            if (_opt_parse_print_help) { \
                exit(_opt_parse_exit_code); \
            } \
            if (!_opt_parse_parsed) { \
                fprintf(stderr, "Error: Unknown argument '%s'\n", _opt_parse_option); \
                _opt_parse_print_help = 1; \
                _opt_parse_exit_code = 1; \
                _opt_parse_index = 0; \
                continue; \
            } \
            if (_opt_parse_has_argument) { \
                ++_opt_parse_index; \
            } \
            ++_opt_parse_index; \
        } \
    } while (0)

#define OPT_FLAG(var, sname, lname, help) \
            do { \
                if (_opt_parse_print_help) { \
                    if (sname) { fprintf(stderr, "%s%s", sname, (lname)? ", ": ""); } \
                    if (lname) { fprintf(stderr, "%s", lname); } \
                    fprintf(stderr, "\n"); \
                    fprintf(stderr, "    %s\n", help) \
                } else { \
                    if ((sname && !strcmp(_opt_parse_option, sname)) || (lname && !strcmp(_opt_parse_option, lname))) { \
                        _opt_parse_parsed = 1; \
                        var = 1; \
                    } \
                } \
            } while (0)

#define OPT_ARG(name, help) \
            do { \
                if (_opt_parse_print_help) { \
                    if (sname) { fprintf(stderr, "Free arguments %s\n", name); } \
                    fprintf(stderr, "    %s\n", help) \
                } else if (_opt_parse_option[0] != '-') { \
                    _opt_parse_parsed = 1; \
                    action(_opt_parse_option); \
                } \
            } while (0)

#define OPT_OPTION_WITH_ARGUMENT(parser, type, sname, lname, help) \
            if (_opt_parse_print_help) { \
                if (sname) { fprintf(stderr, "%s%s", sname, (lname)? ", ": ""); } \
                if (lname) { fprintf(stderr, "%s", lname); } \
                fprintf(stderr, " <%s>\n", type); \
                fprintf(stderr, "    %s\n", help); \
            } else { \
                if (_opt_parse_argument == NULL) { \
                    fprintf(stderr, "Error: options '%s' needs argument.\n", _opt_parse_option); \
                    _opt_parse_print_help = 1; \
                    _opt_parse_exit_code = 1; \
                    continue; \
                } \
                if ((sname && !strcmp(_opt_parse_option, sname)) || (lname && !strcmp(_opt_parse_option, lname))) { \
                    _opt_parse_parsed = 1; \
                    _opt_parse_has_argument = 1; \
                    { \
                        parser; \
                    } \
                } \
            } do {} while(0)


#define OPT_STR(var, sname, lname, help) \
    OPT_OPTION_WITH_ARGUMENT(var = _opt_parse_argument, "str", sname, lname, help)

#define OPT_INT(var, sname, lname, help) \
    OPT_OPTION_WITH_ARGUMENT(char *endptr = NULL; var = strtol(_opt_parse_argument, &endptr, 0); if (endptr == NULL) { _opt_parse_print_help = 1; _opt_parse_exit_code = 1; continue; }, "str", sname, lname, help)


#ifdef __cplusplus
} // extern "C"
#endif

#endif // OPTS_H__INCL
