#include <stdio.h>
#include <stdlib.h>

/*
 * HOMEWORK 0
 * CS3113 - Intro to Operating Systems
 * Professor Christian Grant
 * John Bao Nguyen
 *
 * What: take a number k and a char symbol and draw a diamond
 * 	 of height and width (2*k - 1)
*/

int main(int argc, char *argv[])
{
	// Naive error check
	if (argc != 3)
	{
		fprintf(stderr, "Two parameters an int and a char.\n");
	}
	
	// Get size of diamond via 1st param
	int num = atol(argv[1]);

	if (num < 1 || num > 15) 
	{
		fprintf(stderr, "Then number must be between 1 and 15");
	}

	char *k;
	
	// Set the character k
	k = argv[2];

	/** TODO: fill in here **/

	// Draw upper half of diamond
	for (int i = 0; i < (((2*num) - 1) / 2); i++)
	{
		// Print spaces
		for (int j = 0; j < (2 * num - 1) - i; j++)
		{
			printf(" ");
		}
		
		// Print characters
		for (int j = 0; j < (2 * i) + 1; j++)
		{
			printf("%s", k);	
		}
		printf("\n");

	}
	
	// Draw middle line
	for (int i = 0; i < num; i++) printf(" ");
	for (int j = 0; j < (2 * num) - 1; j++)
	{
		printf("%s", k);
	}

	printf("\n");
		
	// Draw lower half of diamond
	for (int i = (((2*num - 1) / 2)); i >= 0; i--)
	{
		// Print spaces
		for (int j = 0; j < (2 * num - 1) - i; j++)
		{
			printf(" ");
		}

		// Print characters
		for (int j = 0; j < (2 * i) + 1; j++)
		{
			printf("%s", k);
		}
		printf("\n");	
	}

	return 0;
}
