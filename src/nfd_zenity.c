#include "nfd_common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* zenity implementation */

nfdresult_t NFD_OpenDialog(const nfdchar_t *filterList, const nfdchar_t *defaultPath, nfdchar_t **outPath) {
    char command[2048] = "zenity --file-selection";
    
    FILE *fp = popen(command, "r");
    if (!fp) {
        NFDi_SetError("Failed to run zenity");
        return NFD_ERROR;
    }

    char buffer[1024];
    if (fgets(buffer, sizeof(buffer), fp)) {
        // Remove trailing newline
        size_t len = strlen(buffer);
        if (len > 0 && buffer[len-1] == '\n') {
            buffer[len-1] = '\0';
        }
        
        *outPath = (nfdchar_t*)NFDi_Malloc(strlen(buffer) + 1);
        strcpy(*outPath, buffer);
        
        pclose(fp);
        return NFD_OKAY;
    }

    pclose(fp);
    return NFD_CANCEL;
}

nfdresult_t NFD_OpenDialogMultiple(const nfdchar_t *filterList, const nfdchar_t *defaultPath, nfdpathset_t *outPaths) {
    NFDi_SetError("Multiple selection not supported in this zenity wrapper");
    return NFD_ERROR;
}

nfdresult_t NFD_SaveDialog(const nfdchar_t *filterList, const nfdchar_t *defaultPath, nfdchar_t **outPath) {
    NFDi_SetError("Save dialog not supported");
    return NFD_ERROR;
}

nfdresult_t NFD_PickFolder(const nfdchar_t *defaultPath, nfdchar_t **outPath) {
     NFDi_SetError("Pick folder not supported");
    return NFD_ERROR;
}
