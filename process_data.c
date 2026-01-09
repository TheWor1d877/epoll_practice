#include "process_data.h"
#include <stddef.h>
#include <ctype.h>

void process_buffer_data(char buffer[]){
    if(buffer == NULL){
        return;
    }
    for(int i=0;buffer[i]!='\0';i++){
        buffer[i] = toupper((unsigned char)buffer[i]);
    }
}
