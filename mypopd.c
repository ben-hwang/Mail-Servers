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
static int getParameter(char out[]);

int main(int argc, char *argv[]) {
  
  if (argc != 2) {
    fprintf(stderr, "Invalid arguments. Expected: %s <port>\n", argv[0]);
    return 1;
  }
  
  run_server(argv[1], handle_client);
  
  return 0;
}

void handle_client(int fd) {
    
    //current represents current state
    //N : None
    //A : AUTHORIZATION
    //T : TRANSACTION
    //U : UPDATE
    char current = 'N';
    
    //lastCommand represents last command that was issued
    //N : None
    //U : USER
    //P : PASS
    char lastCommand = 'N';
    
    //username of USER parameter is saved as global variable
    //password of PASS parameter is saved as global variable
    char username[256] = "";
    char password[256] = "";
    
    //there must only be one instance of the mail list for any username
    mail_list_t mail_list;
    
    //==============================================================================================================
    
    //send greeting (welcoming) message
    send_string(fd, "+OK POP3 server ready\r\n");
    current = 'A';
    
    //==============================================================================================================
    
    net_buffer_t nb = nb_create(fd, MAX_LINE_LENGTH);
    
    //what client is sending
    char out[MAX_LINE_LENGTH] = "";
    
    //need to store pre-DELE count of mail globally for LIST and QUIT
    int noOfMail;
    
    //==============================================================================================================
    
    //infinite loop that only ends on certain criteria
    while(1) {
        
        //==============================================================================================================
        
        int size = nb_read_line(nb, out);
        
        //Properly replies with error if line is too long
        if (size > MAX_LINE_LENGTH) {
            if (send_string(fd, "-ERR command line too long\r\n") < 0) {
                break;
            }
            
        //Properly closes connection if read (or nb_read_line) returns <= 0
        } else if (size <= 0) {
            break;
        }
        
        //==============================================================================================================
            
        //if command is NOOP
        if (strncasecmp(out, "NOOP", 4) == 0) {
            if (current == 'T') {
                if (send_string(fd, "+OK\r\n") < 0) {
                    break;
                }
                
            } else {
                if (send_string(fd, "-ERR Need to complete AUTHORIZATION\r\n") < 0) {
                    break;
                }
            }
        
        //==============================================================================================================
            
        //if command is USER
        } else if (strncasecmp(out, "USER", 4) == 0) {
            if (current == 'A') {
                
                //rejected if no parameter
                if (strncasecmp(out, "USER\r\n", 6) == 0)  {
                    if (send_string(fd, "-ERR No parameter detected\r\n") < 0) {
                        break;
                    }
                    
                } else {
                    //get username of client
                    strncpy(username, &out[5], strlen(out) - 7);

                    //rejected if user is not in users.txt, otherwise proper reply
                    if (is_valid_user(username, NULL) != 0) {
                        if (send_string(fd, "+OK %s %s\r\n", username, "is a valid mailbox") < 0) {
                            break;
                        }

                        //set last command to USER
                        lastCommand = 'U';

                    } else {
                        if (send_string(fd, "-ERR never heard of %s\r\n", username) < 0) {
                            break;
                        }
                    }
                }
                
            } else {
                if (send_string(fd, "-ERR Bad sequence of commands\r\n") < 0) {
                    break;
                }
            }
            
        //==============================================================================================================
            
        //if command is PASS
        } else if (strncasecmp(out, "PASS", 4) == 0) {
            if (current == 'A' && lastCommand == 'U') {
                
                //rejected if no parameter
                if (strncasecmp(out, "PASS\r\n", 6) == 0) {
                    if (send_string(fd, "-ERR No parameter detected\r\n") < 0) {
                        break;
                    }
                
                } else {
                    
                    //get password of client
                    strncpy(password, &out[5], strlen(out) - 7);
                    
                    //rejected if user/password not in users.txt, otherwise proper reply
                    if (is_valid_user(username, password) != 0) {
                        
                        //load number of mail for user
                        mail_list = load_user_mail(username);
                        noOfMail = get_mail_count(mail_list);
                        
                        if (send_string(fd, "+OK %s%s %d %s\r\n", username, "'s maildrop has", noOfMail, "message(s)") < 0) {
                            break;
                        }
                        
                        //set last command to PASS
                        //set current state to TRANSACTION
                        lastCommand = 'P';
                        current = 'T';
                    
                    } else {
                        if (send_string(fd, "-ERR invalid password\r\n") < 0) {
                            break;
                        }
                    }
                }
                
            } else {
                if (send_string(fd, "-ERR Must input USER first\r\n") < 0) {
                    break;
                }
            }
            
            
        //==============================================================================================================
            
        //if command is STAT
        } else if (strncasecmp(out, "STAT", 4) == 0) {
            if (current == 'T') {
                
                //rejected if parameter specified
                if (strncasecmp(out, "STAT\r\n", 6) == 0) {
                    
                    //load number of mail for user
                    int mail_count = get_mail_count(mail_list);
                    
                    //load total size of mail in bytes
                    int mail_size = get_mail_list_size(mail_list);
                    
                    if (send_string(fd, "+OK %d %d\r\n", mail_count, mail_size) < 0) {
                        break;
                    }
                
                } else {
                    if (send_string(fd, "-ERR Parameter specified\r\n") < 0) {
                        break;
                    }
                }
                
            } else {
                if (send_string(fd, "-ERR Need to complete AUTHORIZATION\r\n") < 0) {
                    break;
                }
            }
            
        //==============================================================================================================
            
        //if command is LIST
        } else if (strncasecmp(out, "LIST", 4) == 0) {
            if (current == 'T') {
                
                //load number of mail for user
                int mail_count = get_mail_count(mail_list);
                
                //load total size of mail in bytes
                int mail_size = get_mail_list_size(mail_list);
                
                //if no parameter specified
                if (strncasecmp(out, "LIST\r\n", 6) == 0) {
                    
                    if (send_string(fd, "+OK %d %s%d %s\r\n", mail_count, "message(s) (", mail_size, "octets)") < 0) {
                        break;
                    }
                    
                    //need to use old mail count to iterate through all elements
                    for (int i = 0; i < noOfMail; i++) {
                        //get size of mail item
                        mail_item_t getItem = get_mail_item(mail_list, i);
             
                        //if item is not deleted
                        if (getItem != NULL) {
                            
                            //get size of each individual mail
                            int ind_mail_size = get_mail_item_size(getItem);
                            
                            if (send_string(fd, "+OK %d %d\r\n", i + 1, ind_mail_size) < 0) {
                                break;
                            }
                        }
                    }
                    //to end multiline replies
                    if (send_string(fd, ".\r\n") < 0) {
                        break;
                    }
                    
                //if parameter specified
                } else {
                    //get parameter of client
                    int result = getParameter(out);
                    
                    //properly replies with single line reply containing number and size
                    if (result != 0) {
                            
                            mail_item_t getItem = get_mail_item(mail_list, result - 1);
                            
                            //if item is deleted or non-existent
                            if (getItem == NULL) {
                                if (send_string(fd, "-ERR No such message\r\n") < 0) {
                                    break;
                                }
                                
                            } else {
                                //get size of mail item
                                int mail_size = get_mail_item_size(getItem);
                                
                                if (send_string(fd, "+OK %d %d\r\n", result, mail_size) < 0) {
                                    break;
                                }
                            }
                    } else {
                        if (send_string(fd, "-ERR Invalid parameter specified\r\n") < 0) {
                            break;
                        }
                    }
                }
                
            //not in transaction stage
            } else {
                if (send_string(fd, "-ERR Need to complete AUTHORIZATION\r\n") < 0) {
                    break;
                }
            }
            
        //==============================================================================================================
            
        //if command is DELE
        } else if (strncasecmp(out, "DELE", 4) == 0) {
            if (current == 'T') {
                //if no parameter specified
                if (strncasecmp(out, "DELE\r\n", 6) == 0) {
                    if (send_string(fd, "-ERR No parameter detected\r\n") < 0) {
                        break;
                    }
                    
                } else {
                    //get parameter of client
                    int result = getParameter(out);
                    
                    if (result != 0) {
                        
                        //load number of mail for user
                        int mail_count = get_mail_count(mail_list);
                        
                        //if non-existent mail
                        if (result > mail_count) {
                            if (send_string(fd, "-ERR No such message\r\n") < 0) {
                                break;
                            }
                        }
                        
                        //mark message deleted
                        mail_item_t getItem = get_mail_item(mail_list, result - 1);
                        
                        if (getItem == NULL) {
                            if (send_string(fd, "-ERR message %d %s\r\n", result, "already deleted") < 0) {
                                break;
                            }
                            
                        } else {
                            mark_mail_item_deleted(getItem);
                            if (send_string(fd, "+OK message %d %s\r\n", result, "deleted") < 0) {
                                break;
                            }
                        }
                        
                    } else {
                        if (send_string(fd, "-ERR Invalid parameter specified\r\n") < 0) {
                            break;
                        }
                    }
                }

            } else {
                if (send_string(fd, "-ERR Need to complete AUTHORIZATION\r\n") < 0) {
                    break;
                }
            }
        
        //==============================================================================================================
            
        //if command is RSET
        } else if (strncasecmp(out, "RSET", 4) == 0) {
            if (current == 'T') {
                
                //rejected if parameter specified
                if (strncasecmp(out, "RSET\r\n", 6) == 0) {
                    //reset deleted fields
                    //number of recovered mails
                    int mail_count = reset_mail_list_deleted_flag(mail_list);
                    
                    if (send_string(fd, "+OK maildrop has %d %s\r\n", mail_count, "message(s)") < 0) {
                        break;
                    }
                    
                } else {
                    if (send_string(fd, "-ERR Invalid parameter specified\r\n") < 0) {
                        break;
                    }
                }

            } else {
                if (send_string(fd, "-ERR Need to complete AUTHORIZATION\r\n") < 0) {
                    break;
                }
            }
        
        //==============================================================================================================
            
        //if command is RETR
        } else if (strncasecmp(out, "RETR", 4) == 0) {
            if (current == 'T') {
                
                //rejected if no parameter specified
                if (strncasecmp(out, "RETR\r\n", 6) == 0) {
                    if (send_string(fd, "-ERR No parameter detected\r\n") < 0) {
                        break;
                    }
                
                } else {
                    //get parameter of client
                    int result = getParameter(out);
        
                    if (result != 0) {
                        
                        //if unable to read message file
                        if (mail_list == NULL) {
                            
                            if (send_string(fd, "-ERR Unable to read message file\r\n") < 0) {
                                break;
                            }
                          
                        //mail_list is loaded properly
                        } else {
                            mail_item_t getItem = get_mail_item(mail_list, result - 1);
                                
                            //if item is deleted or nonexistent
                            if (getItem == NULL) {
                                send_string(fd, "-ERR No such message\r\n");
                                
                            //if item is not deleted and exists
                            } else {
                                //get size of each individual mail
                                int ind_mail_size = get_mail_item_size(getItem);
                                    
                                if (send_string(fd, "+OK %d %s\r\n", ind_mail_size, "octets") < 0) {
                                    break;
                                }
                                    
                                //open a tempfile
                                FILE *tempfile = fopen(get_mail_item_filename(getItem), "r");
                                    
                                char message[1024];
                                    
                                //if the file isn't null, iterate through and send the email message to user
                                if (tempfile != NULL) {
                                    while (fgets(message, 1024, tempfile)) {
                                        send_string(fd, "%s", message);
                                    }
                                        
                                    //close file and send the CLRF to user
                                    fclose(tempfile);
                                    send_string(fd, ".\r\n");
                                    
                                }
                            }
                        }
                        
                    } else {
                        if (send_string(fd, "-ERR Invalid parameter specified\r\n") < 0) {
                            break;
                        }
                    }
                }
                
            } else {
                if (send_string(fd, "-ERR Need to complete AUTHORIZATION\r\n") < 0) {
                    break;
                }
            }
            
        //==============================================================================================================
            
        //if command is QUIT
        } else if (strncasecmp(out, "QUIT", 4) == 0) {
            if (current == 'A') {
                send_string(fd, "+OK dewey POP3 server signing off\r\n");
                break;
                    
            } else if (current == 'T') {
                //delete those marked to be deleted
                destroy_mail_list(mail_list);
                
                //number of mail destroyed (old count - new count)
                int noDestroyed = noOfMail - get_mail_count(mail_list);
                
                if (noDestroyed != 0) {
                    send_string(fd, "+OK dewey POP3 server signing off (%d %s\r\n", noDestroyed, "messages destroyed)");
                } else {
                    send_string(fd, "+OK dewey POP3 server signing off (maildrop empty)\r\n");
                }
                
                current = 'U';
                break;
            }
            
        //==============================================================================================================
            
        } else if (strncasecmp(out, "APOP", 4) == 0 || strncmp(out, "TOP", 3) == 0 || strncmp(out, "UIDL", 4) == 0) {
            if (send_string(fd, "-ERR Command not implemented\r\n") < 0) {
                break;
            }

        //==============================================================================================================
            
        } else {
            if (send_string(fd, "-ERR Syntax error, command unrecognized\r\n") < 0) {
                break;
            }
        }
    }
}

//getParameter will get the parameter for COMMANDS that have parameters with integer types
//Used a char to digit conversion trick, where if you - '0' a char, it gives the digit value
int getParameter(char out[]) {
    
    //get parameter of client
    char parameter[256] = "";
    strncpy(parameter, &out[5], strlen(out) - 7);
    
    //stores digit value of parameter
    int result = 0;
    
    //find number of elements in parameter
    int n = 0;
    char p = ' ';
    while (p!= '\0') {
        n++;
        p = parameter[n];
    }
    
    //small "atoi" function that gets the number in digit value from our parameter
    for (int j = 0 ; j < n; j++) {

        //if char at j is not a digit, then there is an issue -> set result to 0 and return it
        if (parameter[j] < '0' || parameter[j] > '9') {
            result = 0;
            break;
        }
        
        //if char at j is fine, append it to our integer
        result = result*10 + parameter[j] - '0';
    }
    
    return result;
}
