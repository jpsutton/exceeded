#ifndef CONFORM_H
#define CONFORM_H
/* Headless conformance interface (spec README 3.1). argv is the full argument vector. */
int conform_main(const char *data_dir, const char *save_dir, int argc, char **argv, const char *out);
#endif
