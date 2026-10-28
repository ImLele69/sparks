#ifndef KEYBOARD_H
#define KEYBOARD_H

// special keys, outside the printable range
#define KEY_UP     0x01
#define KEY_DOWN   0x02
#define KEY_LEFT   0x03
#define KEY_RIGHT  0x04
#define KEY_DELETE 0x05
#define KEY_HOME   0x06
#define KEY_END    0x07
// never comes from the keyboard: the terminal sends it to make the shell drop the current line
#define KEY_CANCEL 0x0B

void keyboard_init(void);
int keyboard_has_char(void);
char keyboard_getchar(void);

#endif
