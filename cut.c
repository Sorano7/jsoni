#define CUT_IMPL
#include "shared/cut.h"

void flags(CutUnit *u)
{
    cut_unit_flags(u, "-g", "-Wall", "-Wextra", "-Wno-override-init");
    cut_unit_flags(u, "-fsanitize=address,undefined");
}

int main(int argc, char **argv)
{
    cut_build_init();

    CutUnit test;
    cut_unit_init(&test, "test", CUT_UNIT_EXE);
    cut_unit_sources(&test, "tests/parse.c");
    flags(&test);

    cut_build_add(&test);
    return cut_build_run(argc, argv);
}
