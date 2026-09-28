#include <stdio.h>

int main(int argument_count, char **arguments)
{
	FILE *file = NULL;
	if (argument_count != 2 || fopen_s(&file, arguments[1], "wb") != 0) return 1;
	fputs("#define GENERATED_VALUE 42\n", file);
	return fclose(file) != 0;
}
