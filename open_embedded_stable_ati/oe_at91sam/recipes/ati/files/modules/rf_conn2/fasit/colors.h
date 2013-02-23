#ifndef __COLORS_H__
#define __COLORS_H__

#define BRYNN 0

// correct colors:
#if BRYNN
#define black	0
#define red	1
#define green	2
#define yellow	3
#define blue	4
#define magenta 5
#define cyan	6
#define gray	7

//  these are the 'bold/bright' colors
#define BLACK	8
#define RED	9
#define GREEN	10
#define YELLOW	11
#define BLUE	12
#define MAGENTA 13
#define CYAN	14
#define GRAY	15

#else // otherwise must be Nathan

//  incorrrect "Nathan" colors:
#define black	0
#define red	1
#define green	7
#define yellow	5
#define blue	4
#define magenta 3
#define cyan	6
#define gray	2

//  these are the 'bold/bright' colors
#define BLACK	8
#define RED	9
#define GREEN	15
#define YELLOW	13
#define BLUE	12
#define MAGENTA 11
#define CYAN	14
#define GRAY	10

#endif // if brynn vs. nathan

#endif

