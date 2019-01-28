#include <stdio.h>

int main(void)
{
	char * first_name = "John";
	char last_name[] = "Nguyen";

	printf("%s, %s\n", first_name, last_name);
	last_name[0] = 'n';
	printf("%s, %s\n", first_name, last_name);
	return 0;
}
