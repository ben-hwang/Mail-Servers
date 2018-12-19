#include "netbuffer.h"
#include "mailuser.h"
#include "server.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/utsname.h>
#include <ctype.h>

#define MAX_LINE_LENGTH 1024

static void handle_client(int fd);
static void saveMessages(int fd, net_buffer_t nb, char current, user_list_t recipients);

int main(int argc, char *argv[]) {
  
  if (argc != 2) {
    fprintf(stderr, "Invalid arguments. Expected: %s <port>\n", argv[0]);
    return 1;
  }
  
  run_server(argv[1], handle_client);
  
  return 0;
}

//argument is TCP port to listen for client connections
//SMTP used to send email messages
//recipients must be limited to ones supported by system (users.txt)
void handle_client(int fd) {

    //current represents current state
    //N : None
    //H : "HELO"
    //M : "MAIL"
    //R : "RCPT"
    //D : "DATA"
    //E : "END"
    char current = 'N';
    
    //==============================================================================================================
    
    //researched from https://stackoverflow.com/questions/5190553/linux-c-get-server-hostname/5190590
    char hostname[256];
    gethostname(hostname, 256);

    //send greeting (welcoming) message
    send_string(fd, "220 %s %s\r\n", hostname, "Simple Mail Transfer Service Ready");
    
    //==============================================================================================================
    
    //creating our list of recipients (updated in RCPT case)
    user_list_t recipients = create_user_list();
    
    //==============================================================================================================

    net_buffer_t nb = nb_create(fd, MAX_LINE_LENGTH);
    
    //what client is sending
    char out[MAX_LINE_LENGTH] = "";
    
    //==============================================================================================================
    
    //infinite loop that only ends on certain criteria
    while(1) {
        //==============================================================================================================
        
        int size = nb_read_line(nb, out);
        
        //properly replies with 500 error if line is too long
        if (size > MAX_LINE_LENGTH) {
            if (send_string(fd, "500 Syntax error, command line too long\r\n") < 0) {
                break;
            }
        
        //Properly closes connection if read (or nb_read_line) returns <= 0
        } else if (size <= 0) {
            break;
        }
    
        //==============================================================================================================
        
        //if command is QUIT
        if (strncasecmp(out, "QUIT", 4) == 0) {
            if (strlen(out) != 6) {
                //set command as first 4 char of client input
                if (send_string(fd, "455 Server unable to accommodate parameters\r\n") < 0) {
                    break;
                }
                
            //if current state isn't data
            } else if (current != 'D') {
                send_string(fd, "221 %s %s\r\n", hostname, "Service closing transmission channel");
                break;
            }
        
        //==============================================================================================================
            
        //if command is NOOP
        } else if (strncasecmp(out, "NOOP", 4) == 0) {
            //if current state isn't data
            if (current != 'D') {
                if (send_string(fd, "250 OK\r\n") < 0) {
                    break;
                }
            }
            
        //==============================================================================================================
        
        //if command is HELO
        } else if (strncasecmp(out, "HELO", 4) == 0) {
            //if current state isn't None, HELO cannot be sent
            if (current != 'N') {
                if (send_string(fd, "503 Bad sequence of commands\r\n") < 0) {
                    break;
            }

            //Properly replies with 500 if command is valid but is not followed by space
            } else if (strncmp(&out[4], " ", 1) != 0) {
                if (send_string(fd, "500 Syntax error, command is valid but is not followed by space\r\n") < 0) {
                    break;
                }
                
            } else {
                //get domain of client
                //have to initialize or I get garbage characters for some reason
                char domain[256] = "";
                
                strncpy(domain, &out[5], strlen(out) - 5 - 2);
                
                //send HELO response to client
                if (send_string(fd, "250 %s %s%s %s\r\n", "Hello", domain, ", pleased to meet you. I am", hostname) < 0) {
                    break;
                }
                
                //set state as HELO
                current = 'H';
            }
        
        //==============================================================================================================
            
        //if command is MAIL
        } else if (strncasecmp(out, "MAIL", 4) == 0) {
            //if current state is not HELO or DATA
            if (current != 'H' && current != 'D') {
                if (send_string(fd, "503 Bad sequence of commands\r\n") < 0) {
                    break;
                }
                
            //Rejected if parameter is not FROM:<...>\r\n
            } else if (strncasecmp(&out[4], " FROM:<", 7) != 0 || strncasecmp(&out[strlen(out) - 3], ">\r\n", 3) != 0) {
                //for (int i = 0; i < sizeof out; i ++) {
                //printf(" %2x", out[i]);
                //}
                if (send_string(fd, "501 Syntax error in parameters or arguments\r\n") < 0) {
                    break;
                }
                
            } else {
                //get address of client user
                char address[256] = "";
                
                int leftindex = strchr(out,'<') - out + 1;
                int rightindex = strchr(out,'>') - out;
       
                strncpy(address, &out[leftindex], rightindex - leftindex);
                
                //send MAIL response to client
                if (send_string(fd, "250 %s %s\r\n", address, "... Sender ok") < 0) {
                    break;
                }
                
                //set state as MAIL
                current = 'M';
            }

        //==============================================================================================================
            
        //if command is RCPT
        } else if (strncasecmp(out, "RCPT", 4) == 0) {
            //if current state is not MAIL or RCPT
            if (current != 'M' && current != 'R') {
                if (send_string(fd, "503 Bad sequence of commands\r\n") < 0) {
                    break;
                }
            
            //Rejected if parameter is not TO:<...>\r\n
            } else if (strncasecmp(&out[4], " TO:<", 5) != 0 || strncasecmp(&out[strlen(out) - 3], ">\r\n", 3) != 0) {
                if (send_string(fd, "501 Syntax error in parameters or arguments\r\n") < 0) {
                    break;
                }

            } else {
                //get address of recipient
                char address[256] = "";
                
                int leftindex = strchr(out,'<') - out + 1;
                int rightindex = strchr(out,'>') - out;
                
                strncpy(address, &out[leftindex], rightindex - leftindex);
                
                //if recipient is a known user, continue or else send error code
                if (is_valid_user(address, NULL) != 0) {
                    //send RCPT response to client
                    if (send_string(fd, "250 %s %s\r\n", address, "... Recipient ok") < 0) {
                        break;
                    }
                    
                    add_user_to_list(&recipients, address);
                    current = 'R';
                } else {
                    if (send_string(fd, "550 %s %s\r\n", "No such user", address) < 0) {
                        break;
                    }
                    current = 'R';
                }
            }
            
        //==============================================================================================================
            
        //if command is DATA
        } else if (strncasecmp(out, "DATA", 4) == 0) {
            //if current state is not RCPT
            if (current != 'R') {
                if (send_string(fd, "503 Bad sequence of commands\r\n") < 0) {
                    break;
                }
                
            } else if (strlen(out) != 6) {
                //set command as first 4 char of client input
                if (send_string(fd, "455 Server unable to accommodate parameters\r\n") < 0) {
                    break;
                }
                
            } else if (recipients == NULL) {
                if (send_string(fd, "554 No Valid Recipients\r\n") < 0) {
                    break;
                }
                
            } else {
                if (send_string(fd, "354 Start mail input; end with <CRLF>.<CRLF>\r\n") < 0) {
                    break;
                }
                
                current = 'D';
                
                //helper function to save messages
                saveMessages(fd, nb, current, recipients);
                
                if (current == 'D') {
                    if (send_string(fd, "250 OK\r\n") < 0) {
                        break;
                    }
                }
                //set back to HELO state so MAIL can run again
                current = 'H';
                destroy_user_list(recipients);
            }
            
        //==============================================================================================================
            
        } else if (strncasecmp(out, "EHLO", 4) == 0 || strncasecmp(out, "RSET", 4) == 0 || strncasecmp(out, "VRFY", 4) == 0 || strncasecmp(out, "EXPN", 4) == 0 || strncasecmp(out, "HELP", 4) == 0){
            
            if (send_string(fd, "502 Command not implemented\r\n") < 0) {
                break;
            }
        
        } else {
            if (send_string(fd, "500 Syntax error, command unrecognized\r\n") < 0) {
                break;
            }
        }
    }
}

void saveMessages(int fd, net_buffer_t nb, char current, user_list_t recipients) {
    
    //what client is sending
    char out[MAX_LINE_LENGTH] = "";
    
    //out accumulator: accumulates client input
    char out_acc[MAX_LINE_LENGTH] = "";
    
    //keeps track of how much we have written to accumulator
    int out_writer = 0;
    
    if (current == 'D') {
        
        //researched from: https://github.com/perusio/linux-programming-by-example/blob/master/book/ch12/ch12-mkstemp.c
        static char file[] = "tmpXXXXXX";
        
        //Create and open temp file
        int f;
        f = mkstemp(file);
        
        while(nb_read_line(nb, out)) {
            
            //if string is not .\r\n, keep accumulating client input
            if (strncmp(out, ".\r\n", 3) == 0) {
                
                //write what we have accumulated to file
                write(f, out_acc, strlen(out_acc));
                
                save_user_mail(file, recipients);
                
                //Close file
                close(f);

                //Remove it
                unlink(file);
                
                break;
                
            } else {
                
                int out_length = strlen(out);
                strncpy(&out_acc[out_writer], out, out_length);
                out_writer += out_length;
                
            }
        }
    }
}


