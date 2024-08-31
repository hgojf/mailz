#ifndef MAILZ_PRINT_H
#define MAILZ_PRINT_H
int page_letter(int, struct letter *, struct ignore *, 
	struct reorder *, int);

int save_letter(int, struct letter *, struct ignore *, 
	struct reorder *, int);
#endif /* MAILZ_PRINT_H */
