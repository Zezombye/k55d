#ifndef K55D_LOG_H
#define K55D_LOG_H

// Opens the log file next to the executable. The file is truncated at each start.
void log_open(void);
// Writes one line with a timestamp to the log file.
void log_msg(const char *fmt, ...);
// Writes the message to the log, shows it in a message box, then stops the process with exit code 1.
__declspec(noreturn) void fatal(const char *fmt, ...);
// Writes the message to the log and shows it in a message box. The message box does not block the caller.
void alert(const char *fmt, ...);

#endif
