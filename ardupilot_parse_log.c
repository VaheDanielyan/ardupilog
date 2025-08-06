/*
 * ardupilot_parse_log.c - ArduPilot log parser MEX function
 */

#include "mex.h"
#include <string.h>
#include <stdlib.h>

#define MAX_MSG_TYPES 256
#define MAX_MSG_LENGTH 512
#define FMT_ID 128

typedef struct {
    unsigned char type;
    unsigned char length;
    char name[5];
    char format[17];
    char labels[65];
} FmtMessage;

typedef struct {
    unsigned char msg_id;
    size_t *indices;
    unsigned char **data;
    size_t count;
    size_t capacity;
    size_t msg_length;
} MessageData;

typedef struct {
    unsigned char *log_data;
    size_t log_size;
    unsigned char header[2];
    FmtMessage fmt_messages[MAX_MSG_TYPES];
    MessageData messages[MAX_MSG_TYPES];
    size_t fmt_count;
    size_t fmt_length;
    size_t total_msg_count;
} LogParser;



/* Function prototypes */
int findFmtLength(LogParser *parser);
int parseFmtMessages(LogParser *parser);
int parseAllMessages(LogParser *parser, const mxArray *msgFilter);
int validateMessageFilter(const mxArray *msgFilter, LogParser *parser, unsigned char *validIds, size_t *validCount);
int isValidMessage(size_t pos, unsigned char msg_id, size_t msg_len, LogParser *parser);
void addMessageData(MessageData *msgData, size_t index, unsigned char *data, size_t msg_len);
mxArray* createOutputStruct(LogParser *parser);
void cleanupParser(LogParser *parser);

void mexFunction(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
    if (nrhs < 2 || nrhs > 3) {
        mexErrMsgTxt("Usage: logData = ardupilot_parse_log(log_data, header, [msgFilter])");
    }
    if (nlhs > 1) {
        mexErrMsgTxt("Too many output arguments");
    }
    
    LogParser parser;
    memset(&parser, 0, sizeof(LogParser));
    parser.log_data = (unsigned char *)mxGetData(prhs[0]);
    parser.log_size = mxGetNumberOfElements(prhs[0]);
    
    double *header_double = mxGetPr(prhs[1]);
    parser.header[0] = (unsigned char)header_double[0];
    parser.header[1] = (unsigned char)header_double[1];
    
    const mxArray *msgFilter = (nrhs >= 3) ? prhs[2] : NULL;
    
    if (!findFmtLength(&parser)) {
        mexErrMsgTxt("Could not find FMT message length");
    }
    
    if (!parseFmtMessages(&parser)) {
        mexErrMsgTxt("Failed to parse FMT messages");
    }
    
    if (!parseAllMessages(&parser, msgFilter)) {
        mexErrMsgTxt("Failed to parse messages");
    }
    
    plhs[0] = createOutputStruct(&parser);
    cleanupParser(&parser);
}

int findFmtLength(LogParser *parser)
{
    unsigned char search_pattern[3] = {parser->header[0], parser->header[1], FMT_ID};
    
    for (size_t i = 0; i <= parser->log_size - 5; i++) {
        if (memcmp(&parser->log_data[i], search_pattern, 3) == 0) {
            if (parser->log_data[i + 3] == FMT_ID) {
                parser->fmt_length = parser->log_data[i + 4];
                return 1;
            }
        }
    }
    
    parser->fmt_length = 89;
    return 1;
}

int parseFmtMessages(LogParser *parser)
{
    unsigned char search_pattern[3] = {parser->header[0], parser->header[1], FMT_ID};
    parser->fmt_count = 0;
    
    for (size_t i = 0; i <= parser->log_size - parser->fmt_length; i++) {
        if (memcmp(&parser->log_data[i], search_pattern, 3) == 0) {
            if (isValidMessage(i, FMT_ID, parser->fmt_length, parser)) {
                unsigned char *msg_data = &parser->log_data[i + 3];
                
                FmtMessage *fmt = &parser->fmt_messages[parser->fmt_count];
                fmt->type = msg_data[0];
                fmt->length = msg_data[1];
                
                memcpy(fmt->name, &msg_data[2], 4);
                fmt->name[4] = '\0';
                for (int j = 0; j < 4; j++) {
                    if (fmt->name[j] == 0) {
                        fmt->name[j] = '\0';
                        break;
                    }
                }
                
                memcpy(fmt->format, &msg_data[6], 16);
                fmt->format[16] = '\0';
                for (int j = 0; j < 16; j++) {
                    if (fmt->format[j] == 0) {
                        fmt->format[j] = '\0';
                        break;
                    }
                }
                
                memcpy(fmt->labels, &msg_data[22], 64);
                fmt->labels[64] = '\0';
                for (int j = 0; j < 64; j++) {
                    if (fmt->labels[j] == 0) {
                        fmt->labels[j] = '\0';
                        break;
                    }
                }
                
                parser->fmt_count++;
                if (parser->fmt_count >= MAX_MSG_TYPES) {
                    mexWarnMsgTxt("Maximum message types exceeded");
                    break;
                }
            }
        }
    }
    
    return parser->fmt_count > 0;
}

int parseAllMessages(LogParser *parser, const mxArray *msgFilter)
{
    unsigned char validIds[MAX_MSG_TYPES];
    size_t validCount = 0;
    
    if (!validateMessageFilter(msgFilter, parser, validIds, &validCount)) {
        return 0;
    }
    
    /* Initialize message data structures and create lookup table */
    int msgIdToIndex[256]; /* Fast lookup: message ID -> format index */
    memset(msgIdToIndex, -1, sizeof(msgIdToIndex));
    
    for (size_t i = 0; i < parser->fmt_count; i++) {
        FmtMessage *fmt = &parser->fmt_messages[i];
        MessageData *msgData = &parser->messages[i];
        
        /* Check if this message type should be parsed */
        int shouldParse = (validCount == 0); /* Parse all if no filter */
        if (validCount > 0) {
            for (size_t j = 0; j < validCount; j++) {
                if (validIds[j] == fmt->type) {
                    shouldParse = 1;
                    break;
                }
            }
        }
        
        if (!shouldParse) continue;
        
        msgData->msg_id = fmt->type;
        msgData->msg_length = fmt->length;
        msgData->capacity = 1000; /* Initial capacity */
        msgData->indices = (size_t*)mxMalloc(msgData->capacity * sizeof(size_t));
        msgData->data = (unsigned char**)mxMalloc(msgData->capacity * sizeof(unsigned char*));
        msgData->count = 0;
        
        /* Build lookup table for O(1) message type identification */
        msgIdToIndex[fmt->type] = i;
    }
    
    for (size_t pos = 0; pos < parser->log_size - 2; pos++) {
        if (parser->log_data[pos] == parser->header[0] && 
            parser->log_data[pos + 1] == parser->header[1]) {
            
            unsigned char msg_id = parser->log_data[pos + 2];
            int fmt_index = msgIdToIndex[msg_id];
            
            if (fmt_index >= 0) {
                FmtMessage *fmt = &parser->fmt_messages[fmt_index];
                MessageData *msgData = &parser->messages[fmt_index];
                
                if (pos + fmt->length <= parser->log_size &&
                    msgData->indices != NULL &&
                    isValidMessage(pos, msg_id, fmt->length, parser)) {
                    
                    size_t data_len = fmt->length - 3;
                    unsigned char *msg_data = (unsigned char*)mxMalloc(data_len);
                    memcpy(msg_data, &parser->log_data[pos + 3], data_len);
                    
                    addMessageData(msgData, pos + 1, msg_data, data_len);
                    parser->total_msg_count++;
                    
                    
                    pos += fmt->length - 1;
                }
            }
        }
    }
    
    return 1;
}

int validateMessageFilter(const mxArray *msgFilter, LogParser *parser, unsigned char *validIds, size_t *validCount)
{
    *validCount = 0;
    
    if (msgFilter == NULL || mxIsEmpty(msgFilter)) {
        return 1;
    }
    
    if (mxIsCell(msgFilter)) {
        size_t numFilters = mxGetNumberOfElements(msgFilter);
        
        for (size_t i = 0; i < numFilters; i++) {
            mxArray *nameCell = mxGetCell(msgFilter, i);
            if (!mxIsChar(nameCell)) continue;
            
            char *filterName = mxArrayToString(nameCell);
            for (size_t j = 0; j < parser->fmt_count; j++) {
                if (strcmp(parser->fmt_messages[j].name, filterName) == 0) {
                    validIds[*validCount] = parser->fmt_messages[j].type;
                    (*validCount)++;
                    break;
                }
            }
            
            mxFree(filterName);
        }
    } else if (mxIsNumeric(msgFilter)) {
        /* Numeric array of message IDs */
        double *filterIds = mxGetPr(msgFilter);
        size_t numFilters = mxGetNumberOfElements(msgFilter);
        
        for (size_t i = 0; i < numFilters; i++) {
            unsigned char msgId = (unsigned char)filterIds[i];
            
            /* Verify this message ID exists */
            for (size_t j = 0; j < parser->fmt_count; j++) {
                if (parser->fmt_messages[j].type == msgId) {
                    validIds[*validCount] = msgId;
                    (*validCount)++;
                    break;
                }
            }
        }
    }
    
    return 1;
}

int isValidMessage(size_t pos, unsigned char msg_id, size_t msg_len, LogParser *parser)
{
    /* Check if we have enough bytes for the complete message */
    if (pos + msg_len > parser->log_size) {
        return 0;
    }
    
    /* Check if next message has valid header (or we're at end of log) */
    size_t next_pos = pos + msg_len;
    if (next_pos >= parser->log_size) {
        return 1; /* End of log is valid */
    }
    
    if (next_pos + 1 < parser->log_size) {
        return (parser->log_data[next_pos] == parser->header[0] && 
                parser->log_data[next_pos + 1] == parser->header[1]);
    }
    
    return 1;
}

void addMessageData(MessageData *msgData, size_t index, unsigned char *data, size_t msg_len)
{
    /* Expand capacity if needed */
    if (msgData->count >= msgData->capacity) {
        msgData->capacity *= 2;
        msgData->indices = (size_t*)mxRealloc(msgData->indices, msgData->capacity * sizeof(size_t));
        msgData->data = (unsigned char**)mxRealloc(msgData->data, msgData->capacity * sizeof(unsigned char*));
    }
    
    msgData->indices[msgData->count] = index;
    msgData->data[msgData->count] = data;
    msgData->count++;
}

mxArray* createOutputStruct(LogParser *parser)
{
    const char *fieldNames[] = {"fmt_messages", "message_data", "message_indices", 
                               "message_names", "total_messages", "fmt_length"};
    mxArray *output = mxCreateStructMatrix(1, 1, 6, fieldNames);
    
    /* Create FMT messages array */
    const char *fmtFields[] = {"type", "length", "name", "format", "labels"};
    mxArray *fmtArray = mxCreateStructMatrix(parser->fmt_count, 1, 5, fmtFields);
    
    for (size_t i = 0; i < parser->fmt_count; i++) {
        FmtMessage *fmt = &parser->fmt_messages[i];
        
        mxSetField(fmtArray, i, "type", mxCreateDoubleScalar(fmt->type));
        mxSetField(fmtArray, i, "length", mxCreateDoubleScalar(fmt->length));
        mxSetField(fmtArray, i, "name", mxCreateString(fmt->name));
        mxSetField(fmtArray, i, "format", mxCreateString(fmt->format));
        mxSetField(fmtArray, i, "labels", mxCreateString(fmt->labels));
    }
    mxSetField(output, 0, "fmt_messages", fmtArray);
    
    /* Create message data cell arrays */
    mxArray *dataCell = mxCreateCellMatrix(parser->fmt_count, 1);
    mxArray *indicesCell = mxCreateCellMatrix(parser->fmt_count, 1);
    mxArray *namesCell = mxCreateCellMatrix(parser->fmt_count, 1);
    
    for (size_t i = 0; i < parser->fmt_count; i++) {
        MessageData *msgData = &parser->messages[i];
        FmtMessage *fmt = &parser->fmt_messages[i];
        
        /* Set message name */
        mxSetCell(namesCell, i, mxCreateString(fmt->name));
        
        if (msgData->count > 0) {
            /* Create indices array */
            mxArray *indices = mxCreateDoubleMatrix(1, msgData->count, mxREAL);
            double *indicesPtr = mxGetPr(indices);
            for (size_t j = 0; j < msgData->count; j++) {
                indicesPtr[j] = msgData->indices[j];
            }
            mxSetCell(indicesCell, i, indices);
            
            /* Create data matrix */
            size_t dataLen = msgData->msg_length - 3;
            mxArray *data = mxCreateNumericMatrix(dataLen, msgData->count, mxUINT8_CLASS, mxREAL);
            unsigned char *dataPtr = (unsigned char*)mxGetData(data);
            
            for (size_t j = 0; j < msgData->count; j++) {
                memcpy(&dataPtr[j * dataLen], msgData->data[j], dataLen);
            }
            mxSetCell(dataCell, i, data);
        } else {
            /* Empty arrays for messages with no data */
            mxSetCell(indicesCell, i, mxCreateDoubleMatrix(0, 0, mxREAL));
            mxSetCell(dataCell, i, mxCreateNumericMatrix(0, 0, mxUINT8_CLASS, mxREAL));
        }
    }
    
    mxSetField(output, 0, "message_data", dataCell);
    mxSetField(output, 0, "message_indices", indicesCell);
    mxSetField(output, 0, "message_names", namesCell);
    mxSetField(output, 0, "total_messages", mxCreateDoubleScalar(parser->total_msg_count));
    mxSetField(output, 0, "fmt_length", mxCreateDoubleScalar(parser->fmt_length));
    
    return output;
}

void cleanupParser(LogParser *parser)
{
    for (size_t i = 0; i < parser->fmt_count; i++) {
        MessageData *msgData = &parser->messages[i];
        if (msgData->indices) {
            mxFree(msgData->indices);
        }
        if (msgData->data) {
            for (size_t j = 0; j < msgData->count; j++) {
                if (msgData->data[j]) {
                    mxFree(msgData->data[j]);
                }
            }
            mxFree(msgData->data);
        }
    }
}