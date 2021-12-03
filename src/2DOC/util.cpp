
#include "util.h"

uint64_t round_up_power2_uint64(uint64_t n) {
	n--;
	while (n & (n - 1)) n = n & (n - 1);
	return n << 1;
}

// Returning the log2 of the input number which must itself be power of two
// Report error and exit if not; The name is a string for the error message
int util_log2_int32(int num, const char *name) {
  if(popcount_int32(num) != 1) {
    error_exit("\"%s\" must be a power of two (see %d)\n", name, num);
  }
  return __builtin_ffs(num) - 1;
}

int util_log2_uint64(uint64_t num, const char *name) {
  if(popcount_uint64(num) != 1) {
    error_exit("\"%s\" must be a power of two (see %lu)\n", name, num);
  }
  return __builtin_ffs(num) - 1;
}

uint64_t addr_gen(uint64_t page, uint64_t cache, uint64_t offset) {
  assert(cache < (0x1 << (UTIL_PAGE_BITS - UTIL_CACHE_LINE_BITS)));
  assert(offset < (0x1 << UTIL_CACHE_LINE_BITS));
  return (page << UTIL_PAGE_BITS) | (cache << UTIL_CACHE_LINE_BITS) | offset;
}

void assert_int32_range(int num, int low, int high, const char *name) {
  if(num < low || num > high) error_exit("\"%s\" must be within range [%d, %d] (see %d)\n", name, low, high, num);
}
void assert_int32_power2(int num, const char *name) {
  if(popcount_int32(num) != 1) error_exit("\"%s\" must be a power of two (see %d)\n", name, num);
}
void assert_uint64_range(uint64_t num, uint64_t low, uint64_t high, const char *name) {
  if(num < low || num > high) error_exit("\"%s\" must be within range [%lu, %lu] (see %lu)\n", name, low, high, num);
}
void assert_uint64_power2(uint64_t num, const char *name) {
  if(popcount_uint64(num) != 1) error_exit("\"%s\" must be a power of two (see %lu)\n", name, num);
}

//* string functions

char *strclone(const char *s) {
  int len = strlen(s);
  char *ret = (char *)malloc(len + 1);
  SYSEXPECT(ret != NULL);
  strcpy(ret, s);
  return ret;
}

char *strnclone(const char *s, int n) {
  char *ret = (char *)malloc(n + 1);
  SYSEXPECT(ret != NULL);
  strncpy(ret, s, n); // This will NOT auto-pad '\0' if src is longer than n
  ret[n] = '\0';
  return ret;
}

// Undefined if times <= 0
char *strfind(const char *s, const char *sub, int times) {
  assert(times > 0);
  while(1) {
    s = strstr(s, sub);
    if(s == NULL || times == 1) {
      break;
    }
    times--;
    s++;
  }
  return (char *)s;
}

convertq_t convert_queue;

// Removes trailing zeros for floating point numbers; Integers remain the same
static void crop_trailing_zero(char *buf) {
  char *p = buf;
  int after_point = 0; // Whether we have seen decimal point
  char *point = NULL;
  while(*p) {
    if(*p == '.') {
      after_point = 1;
      point = p;
    } else if(after_point && *p == '0') {
      int see_nonzero = 0;
      char *q = p;
      while(*q != '\0') {
        if(*q != '0') {
          see_nonzero = 1;
          break;
        }
        q++;
      }
      if(see_nonzero == 0) {
        *p = '\0';
        break;
      }
    }
    p++;
  }
  // Avoid trailing decimal point
  if(point[1] == '\0') point[0] = '\0';
  return;
}

const char *to_abbr(uint64_t n) {
  char *buf = convertq_advance();
  if(n >= 1000000UL) {
    snprintf(buf, CONVERT_BUFFER_SIZE - 1, "%.3lf", (double)n / 1000000.0);
    crop_trailing_zero(buf);
    strcat(buf, "M");
  } else if(n >= 1000UL) {
    snprintf(buf, CONVERT_BUFFER_SIZE - 1, "%.3lf", (double)n / 1000.0);
    crop_trailing_zero(buf);
    strcat(buf, "K");
  } else {
    sprintf(buf, "%lu", n);
  }
  return buf;
}

// Convert uint64_t to size representation (using KB, MB, GB suffix)
const char *to_size(uint64_t n) {
  char *buf = convertq_advance();
  if(n >= 1024UL * 1024UL * 1024UL) {
    snprintf(buf, CONVERT_BUFFER_SIZE - 2, "%.3lf", (double)n / (1024.0 * 1024.0 * 1024.0));
    crop_trailing_zero(buf);
    strcat(buf, "GB");
  } else if(n >= 1024UL * 1024UL) {
    snprintf(buf, CONVERT_BUFFER_SIZE - 2, "%.3lf", (double)n / (1024.0 * 1024.0));
    crop_trailing_zero(buf);
    strcat(buf, "MB");
  } else if(n >= 1024UL) {
    snprintf(buf, CONVERT_BUFFER_SIZE - 2, "%.3lf", (double)n / 1024.0);
    crop_trailing_zero(buf);
    strcat(buf, "KB");
  } else {
    sprintf(buf, "%lu", n);
  }
  return buf;
}

//* conf_t

// There is no guarantee that k and v are zero terminating strings
void conf_insert(conf_t *conf, const char *k, const char *v, int klen, int vlen, int line) {
  conf_node_t *node = (conf_node_t *)malloc(sizeof(conf_node_t));
  SYSEXPECT(node != NULL);
  memset(node, 0x00, sizeof(conf_node_t));
  node->key = (char *)malloc(klen + 1);
  SYSEXPECT(node->key != NULL);
  memcpy(node->key, k, klen);
  node->key[klen] = '\0';
  node->value = (char *)malloc(vlen + 1);
  SYSEXPECT(node->value != NULL);
  memcpy(node->value, v, vlen);
  node->value[vlen] = '\0';
  // Test for duplication
  if(conf_find(conf, node->key) != NULL) {
    error_exit("Duplicated option \"%s = %s\" on line %d\n", node->key, node->value, line);
  }
  node->next = conf->head;
  node->line = line;
  conf->head = node;
  conf->item_count++;
  return;
}

// Both k and v are null-terminated string
void conf_insert_ext(conf_t *conf, const char *k, const char *v) {
  conf_insert(conf, k, v, strlen(k), strlen(v), -1);
  return;
}

// Handles directives starting with "%"
// buf points to the "%" character
// Supported directives:
//   %include <filename> - As if the content of <filename> is copied to the current file
void conf_init_directive(conf_t *conf, char *buf, int curr_line) {
  assert(buf[0] == '%');
  int len = strlen(buf);
  // End of the directive's command, either the entire line, or till the first space
  int cmd_end_index = len;
  for(int i = 1;i < len;i++) {
    if(isspace(buf[i])) {
      cmd_end_index = i;
      break;
    }
  }
  if(cmd_end_index == 1) {
    error_exit("Empty directive (line %d)\n", curr_line);
  }
  // Copy to a temp buffer; Length is "cmd_end_index - 1" and we add the terminating zero
  char cmd[cmd_end_index];
  memcpy(cmd, buf + 1, cmd_end_index - 1);
  cmd[cmd_end_index - 1] = '\0';
  if(streq(cmd, "include") == 1) {
    int name_start_index = cmd_end_index;
    while(name_start_index < len && isspace(buf[name_start_index])) {
      name_start_index++;
    }
    if(name_start_index == len) {
      error_exit("There is no file name for %%include directive (line %d)\n", curr_line);
    } else if(buf[name_start_index] == '\"' || buf[name_start_index] == '\'') {
      error_exit("%%include directive's file name does not need quotation mark (line %d)\n", curr_line);
    }
    int name_end_index = name_start_index;
    while(name_end_index < len && !isspace(buf[name_end_index])) {
      name_end_index++;
    }
    int filename_len = name_end_index - name_start_index;
    char filename[filename_len + 1];
    memcpy(filename, buf + name_start_index, filename_len);
    filename[filename_len] = '\0';
    // Recursively create a temp conf
    conf_t *include_conf = conf_init(filename);
    // Copy all kv pairs to the current conf
    conf_node_t *curr = include_conf->head;
    while(curr != NULL) {
      conf_insert(conf, curr->key, curr->value, strlen(curr->key), strlen(curr->value), curr->line);
      curr = curr->next;
    }
    conf_free(include_conf);
  } else {
    error_exit("Unknown directive: \"%s\" (line %d)\n", cmd, curr_line);
  }
  return;
}

// Reads a line from fp and return it in a buffer allocated on the heap
// This function guarantees an entire line ending with \n or EOF (in which case there is no \n at the end)
// Caller is responsible for freeing the returned buffer
// Returns NULL if the read position is at EOF when this function is called. This condition does not 
// need to be triggered by an attempt to read
// This function will *NEVER* return a string of length zero
// This function will convert CRLF to LF, if there is \r\n at the end of the line
char *conf_fgets(FILE *fp) {
  int buf_size = CONF_FGETS_INIT_SIZE; // Initial size of the buffer
  int curr_len = 0;    // Current length of the string in the buffer, including '\n'
  char *ret = (char *)malloc(buf_size);
  SYSEXPECT(ret != NULL);
  // If the previous call sets EOF, quick return here
  if(feof(fp)) {
    //printf("Previously triggered EOF\n");
    free(ret);
    return NULL;
  }
  int first_loop = 1;
  while(1) {
    int fp_offset = ftell(fp);
    // This will trigger EOF if it actually attempts to read past EOF
    char *fgets_ret = fgets(ret + curr_len, buf_size - curr_len, fp);
    //printf("buf \"%s\"\n", ret);
    // This only happens when the fp is right at EOF after the previous loop. fgets() will
    // attempt to read, which sets EOF without new content.
    if(fgets_ret == NULL) {
      // The read position is already at EOF when this function is called
      if(first_loop == 1) {
        //printf("First loop EOF\n");
        free(ret);
        return NULL;
      } else {
        //printf("Later loop EOF\n");
        // Something has been read from previous iterations
        break;
      }
    }
    first_loop = 0;
    // Number of raw bytes read, including '\n'
    int fp_read_size = ftell(fp) - fp_offset;
    assert(fp_read_size > 0);
    curr_len += fp_read_size;
    // There is always a '\0' appended by fgets()
    assert(curr_len > 0 && curr_len < buf_size && ret[curr_len] == '\0');
    // It is an actual '\n', not because the buffer is not big enough
    if(ret[curr_len - 1] == '\n') {
      // \r\n is transformed to \n
      if(curr_len >= 2 && ret[curr_len - 2] == '\r') {
        ret[curr_len - 2] = '\n';
        ret[curr_len - 1] = '\0';
        curr_len--;
      }
      break;
    }
    buf_size *= 2;
    // Otherwise, the buffer is not big enough, so we realloc()
    ret = (char *)realloc(ret, buf_size);
    SYSEXPECT(ret != NULL);
  }
  return ret;
}

// Removes everything after '#' and returns the length of the new character
// The return value can be zero
int conf_remove_comment(char *s) {
  int len = strlen(s);
  // Remove comments
  for(int i = 0;i < len;i++) {
    // If comment symbol is seen, then the line terminates here
    if(s[i] == '#') {
      // Update length as well
      len = i;
      s[i] = '\0';
      break;
    }
  }
  return len;
}

// Trim left and right spaces and move the content such that the first character 
// is non-space.
int conf_trim(char *s) {
  int len = strlen(s);
  char *begin = s;
  // This points to the last character of the current string
  char *end = s + (len - 1);
  // Move begin until end of string or non-space
  while(*begin != '\0' && isspace(*begin)) {
    begin++;
  }
  // Entire string is space
  if(*begin == '\0') {
    s[0] = '\0';
    return 0;
  }
  assert(end >= begin);
  while(end >= begin && isspace(*end)) {
    end--;
  }
  assert(end >= begin);
  // New length
  len = end + 1 - begin;
  // Potentially overlapping memory copy
  memmove(s, begin, len);
  s[len] = '\0';
  return len;
}

// Reads a logical line, i.e., with spaces trimmed, no comments, and combines line continuation.
// Line number is incremented for each line read. Each continuation is treated as one line.
// It is the next line to be read in the input stream
// This function handles (in this order):
//   (1) Comments "#"
//   (2) Spaces before the first and after the last valid character
//   (3) Empty lines
//   (4) Escaped new lines "\\\n"
char *conf_read_line(FILE *fp, int *line_number) {
  char *ret = NULL;
  while(1) {
    char *curr_s = conf_fgets(fp);
    if(curr_s == NULL) {
      break;
    }
    (*line_number)++;
    //printf("curr_s \"%s\"\n", curr_s);
    int len;
    len = conf_remove_comment(curr_s);
    int line_cont = 0;
    // Check line continuation. This must be done before string trimming
    if(len >= 2 && curr_s[len - 1] == '\n' && curr_s[len - 2] == '\\') {
      // Also delete the continuation mark
      curr_s[len - 2] = '\0';
      line_cont = 1;
    }
    len = conf_trim(curr_s);
    // Skip empty lines
    if(len == 0) {
      free(curr_s);
      if(ret == NULL || line_cont == 1) {
        // If nothing has been read, just try the next line
        // Or, there is a line continuation
        continue;  
      } else {
        // Otherwise, we have completed reading
        break;
      }
    }
    // Append the current line to ret
    if(ret == NULL) {
      ret = curr_s;
    } else {
      ret = (char *)realloc(ret, strlen(ret) + len + 1);
      strcat(ret, curr_s);
      free(curr_s);
    }
    //printf("line_cont = %d\n", line_cont);
    // Check for line continuation, \\\n
    if(line_cont == 0) {
      break;
    }
  }
  return ret;
}

conf_t *conf_init(const char *filename) {
  conf_t *conf = conf_init_empty();
  assert(conf->filename == NULL);
  // Passing NULL as the second argument forces the lib call to allocate the buffer using malloc()
  conf->filename = realpath(filename, NULL);
  SYSEXPECT(conf->filename != NULL);
  FILE *fp = fopen(filename, "r");
  SYSEXPECT(fp != NULL);
  int next_line = 0;
  while(1) {
    // Backup this value
    int curr_line = next_line;
    // This will set curr_line to the next line to be read
    char *line = conf_read_line(fp, &next_line);
    if(line == NULL) {
      break;
    }
    int len = strlen(line);
    assert(len > 0);
    if(line[0] == '%') { // Check whether it is directive, which has a "%" before the line
      conf_init_directive(conf, line, curr_line);
    } else {
      int assign_index = -1; // The index of the "=" symbol
      for(int i = 0;i < len;i++) if(line[i] == '=') assign_index = i;
      if(assign_index == -1) {
        //printf("\"%s\"\n", line);
        error_exit("Did not find \"=\" sign on line %d (file \"%s\")\n", curr_line, filename);
      }
      // Last character of the key; This could be (line - 1) if the first char of the line is '='
      char *key_end = line + assign_index - 1;
      while(key_end >= line && isspace(*key_end)) {
        key_end--;
      }
      if(key_end <= line) {
        error_exit("Missing key on line %d (file \"%s\")\n", curr_line, filename);
      }
      // This could point to '\0'
      char *value_begin = line + assign_index + 1;
      while(*value_begin != '\0' && isspace(*value_begin)) {
        value_begin++;
      }
      if(*value_begin == '\0') {
        error_exit("Missing value on line %d (file \"%s\")\n", curr_line, filename);
      }
      int key_len = key_end - line + 1;
      // Do not need +1 here since line + strlen() will stop at the '\0'
      int value_len = line + len - value_begin;
      conf_insert(conf, line, value_begin, key_len, value_len, curr_line);
    }
    free(line);
  }
  fclose(fp);
  return conf;
}

conf_t *conf_init_empty() {
  conf_t *conf = (conf_t *)malloc(sizeof(conf_t));
  SYSEXPECT(conf != NULL);
  memset(conf, 0x00, sizeof(conf_t));
  return conf;
}

conf_t *conf_init_from_str(const char *s) {
  char name_buf[256];
  // Generate a random file name using current time as the
  snprintf(name_buf, sizeof(name_buf), "_conf_temp_%lu_%lu.txt", 
    (uint64_t)time(NULL), (uint64_t)__builtin_ia32_rdtsc());
  FILE *fp = fopen(name_buf, "w");
  SYSEXPECT(fp != NULL);
  int len = strlen(s);
  int write_ret = fwrite(s, 1, len, fp);
  if(write_ret != len) {
    error_exit("Error writing the temp file; Returned %d expected %d\n", write_ret, len);
  }
  fclose(fp);
  conf_t *conf = conf_init(name_buf);
  int rm_ret = remove(name_buf);
  SYSEXPECT(rm_ret == 0);
  return conf;
}

void conf_node_free(conf_node_t *node) {
  free(node->key);
  free(node->value);
  free(node);
  return;
}

void conf_node_update_value(conf_node_t *node, const char *new_value) {
  free(node->value);
  node->value = strclone(new_value);
  return;
}

void conf_free(conf_t *conf) {
  conf_node_t *curr = conf->head;
  while(curr) {
    conf_node_t *prev = curr;
    curr = curr->next;
    conf_node_free(prev);
  }
  if(conf->filename != NULL) free(conf->filename);
  free(conf);
}

// Returns the node if key exists; NULL if does not exist
conf_node_t *conf_find(conf_t *conf, const char *key) {
  conf_node_t *curr = conf->head;
  while(curr) {
    if(streq(curr->key, key)) {
      curr->accessed = 1;
      return curr;
    }
    curr = curr->next;
  }
  return NULL;
}

int conf_remove(conf_t *conf, const char *key) {
  conf_node_t *node = conf->head;
  if(node == NULL) {
    return 0;
  } else if(streq(node->key, key)) {
    conf->head = node->next;
    conf_node_free(node);
    conf->item_count--;
    return 1;
  }
  while(node->next) {
    if(streq(node->next->key, key)) {
      conf_node_t *rm_node = node->next;
      node->next = rm_node->next;
      conf_node_free(rm_node);
      conf->item_count--;
      return 1;
    }
    node = node->next;
  }
  return 0;
}

int conf_rewrite(conf_t *conf, const char *key, const char *value) {
  conf_node_t *node = conf_find(conf, key);
  if(node == NULL) return 0;
  conf_node_update_value(node, value);
  return 1;
}

int conf_find_str(conf_t *conf, const char *key, char **ret) {
  conf_node_t *node = conf_find(conf, key);
  if(node == NULL) return 0;
  *ret = node->value;
  return 1;
}

// Returns 1 if key exists and converts successfully
// Returns 0 if key does not exist
// Report error if key exists but is not a legal integer
int conf_find_int32(conf_t *conf, const char *key, int *ret) {
  conf_node_t *node = conf_find(conf, key);
  if(node == NULL) return 0;
  char buf[32];
  int t = atoi(node->value);
  sprintf(buf, "%d", t);
  if(streq(node->value, buf) == 0) 
    error_exit("Illegal integer literal (line %d): \"%s\" (%s)\n", node->line, node->value, buf);
  *ret = t;
  return 1;
}

int conf_find_uint64(conf_t *conf, const char *key, uint64_t *ret) {
  conf_node_t *node = conf_find(conf, key);
  if(node == NULL) return 0;
  char *end;
  // "0" means we do not care the base, just let format determine
  uint64_t t = strtoul(node->value, &end, 0);
  // The end pointer will be set to the next char after the decoded string
  if(*end != '\0') 
    error_exit("Illegal integer literal (line %d): \"%s\"\n", node->line, node->value);
  *ret = t;
  return 1;
}

int conf_find_bool(conf_t *conf, const char *key, int *ret) {
  conf_node_t *node = conf_find(conf, key);
  if(node == NULL) return 0;
  if(streq(node->value, "true") || streq(node->value, "1")) {
    *ret = 1;
  } else if(streq(node->value, "false") || streq(node->value, "0")) {
    *ret = 0;
  } else {
    error_exit("Option \"%s\" must have a boolean value (\"true\" or \"false\", see \"%s\")\n", 
      key, node->value);
  }
  return 1;
}

int conf_find_uint64_size(conf_t *conf, const char *key, uint64_t *ret) {
  conf_node_t *node = conf_find(conf, key);
  if(node == NULL) return 0;
  char *end;
  // Do not pass end pointer; Use the format of the string to determine base
  uint64_t t = strtoul(node->value, &end, 0);
  // If no number ever gets parsed then report error
  if(end == node->value) {
    error_exit("Illegal size value: \"%s\"\n", node->value);
  } else if(*end == '\0') {
    *ret = t;
    return 1; // If no scale is given we take it as byte
  }
  while(isspace(*end)) end++; // Jump over spaces
  // Then determine the scale
  if(streq(end, "B") || streq(end, "Byte") || streq(end, "byte")) {
    *ret = t;
  } else if(streq(end, "KB") || streq(end, "kb")) {
    *ret = t * 1024UL;
  } else if(streq(end, "MB") || streq(end, "mb")) {
    *ret = t * 1024UL * 1024UL;
  } else if(streq(end, "GB") || streq(end, "gb")) {
    *ret = t * 1024UL * 1024UL * 1024UL;
  } else {
    error_exit("Illegal size value: \"%s\"\n", node->value);
  }
  return 1;
}

int conf_find_uint64_abbr(conf_t *conf, const char *key, uint64_t *ret) {
  conf_node_t *node = conf_find(conf, key);
  if(node == NULL) return 0;
  char *end;
  uint64_t t = strtoul(node->value, &end, 0);
  if(end == node->value) {
    error_exit("Illegal integer literal: \"%s\"\n", node->value);
  } else if(*end == '\0') {
    *ret = t;
    return 1;
  }
  while(isspace(*end)) end++; // Jump over spaces
  // Then determine the scale
  if(streq(end, "K") || streq(end, "k")) {
    *ret = t * 1000UL;
  } else if(streq(end, "M") || streq(end, "m")) {
    *ret = t * 1000UL * 1000UL;
  } else if(streq(end, "B") || streq(end, "b")) {
    *ret = t * 1000UL * 1000UL * 1000UL;
  } else {
    error_exit("Could not recognize abbreviation \"%s\" after numeric value \"%lu\"\n", end, t);
  }
  return 1;
}

// Does not copy the string; Just return it
char *conf_find_str_mandatory(conf_t *conf, const char *key) {
  char *value;
  int ret = conf_find_str(conf, key, &value);
  if(ret == 0) error_exit("Option \"%s\" is not found in the configuration\n", key);
  return value;
}

int conf_find_bool_mandatory(conf_t *conf, const char *key) {
  int value;
  int ret = conf_find_bool(conf, key, &value);
  if(ret == 0) {
    error_exit("Option \"%s\" is not found in the configuration\n", key);
  }
  return value;
}

// 1. Key must exist
// 2. Value must be in the given range [low, high], if CONF_RANGE is on
// 3. Options can carry extra condition such range, as power of two, abbr, etc.
int conf_find_int32_range(conf_t *conf, const char *key, int low, int high, int options) {
  int num;
  if((options & CONF_ABBR) || (options & CONF_SIZE)) {
    error_exit("CONF_ABBR and CONF_SIZE not supported\n");
  }
  int ret = conf_find_int32(conf, key, &num);
  if(ret == 0) error_exit("Configuration \"%s\" does not exist\n", key);
  if(options & CONF_RANGE) assert_int32_range(num, low, high, key);
  if(options & CONF_POWER2) assert_int32_power2(num, key);
  return num;
}

// The range is given in [low, high], with CONF_RANGE in options
uint64_t conf_find_uint64_range(conf_t *conf, const char *key, uint64_t low, uint64_t high, int options) {
  uint64_t num;
  if((options & CONF_ABBR) && (options & CONF_SIZE)) {
    error_exit("CONF_ABBR and CONF_SIZE should not co-exit\n");
  }
  int ret;
  if(options & CONF_ABBR) ret = conf_find_uint64_abbr(conf, key, &num);
  else if(options & CONF_SIZE) ret = conf_find_uint64_size(conf, key, &num);
  else ret = conf_find_uint64(conf, key, &num);
  if(ret == 0) error_exit("Configuration \"%s\" does not exist\n", key);
  if(options & CONF_RANGE) assert_uint64_range(num, low, high, key);
  if(options & CONF_POWER2) assert_uint64_power2(num, key);
  return num;
}

int conf_find_comma_list_str(conf_t *conf, const char *key, char ***list, int *count) {
  conf_node_t *node = conf_find(conf, key);
  *count = 0;
  if(node == NULL) {
    *list = NULL;
    return 0;
  }
  // Count number of commas
  int comma_count = 0;
  char *s = node->value;
  while(*s != '\0') {
    if(*s == ',') {
      comma_count++;
    }
    s++;
  }
  *count = comma_count + 1;
  *list = (char **)malloc(sizeof(char *) * (*count));
  SYSEXPECT(list != NULL);
  s = node->value;
  for(int i = 0;i < *count;i++) {
    // Space before string
    s = conf_str_skip_space(s);
    // Until comma or space
    char *end = s;
    while(*end != '\0' && *end != ',') {
      end++;
    }
    char *value_end = end;
    while(value_end > s && isspace(*(value_end - 1))) {
      value_end--;
    }
    if(value_end == s) {
      error_exit("Empty string in the comma list (line %d index %d)\n", node->line, i);
    }
    // The substring is [s, end)
    (*list)[i] = strnclone(s, value_end - s);
    s = end;
    // Skip comma, if not the last one
    if(i != *count - 1) {
      assert(*s == ',');
      s++;
    }
  }
  return 1;
}

// If key not found, set list and count to NULL and return 0
// Otherwise, store all uint64_t values in heap memory and store the pointer in list
int conf_find_comma_list_uint64(conf_t *conf, const char *key, uint64_t **list, int *count) {
  conf_node_t *node = conf_find(conf, key);
  *count = 0;
  if(node == NULL) {
    *list = NULL;
    return 0;
  }
  char *value = conf_str_skip_space(node->value);
  // First iteration checks validity and computes count
  int index = 0;
  while(*value != '\0') {
    char *end;
    // Invariant: value must point to non-space character, and at least one char must be 
    // read in order for the conversion to be valid. So we know the conversion is invalid
    // if end equals value
    strtoul(value, &end, 0);
    if(end == value) {
      error_exit("No valid comma-list conversion can be performed (line %d index %d)\n", node->line, index);
    }
    (*count)++;
    // Skip white spaces after a parsed number and check for commas
    value = conf_str_skip_space(end);
    if(*value == ',') {
      value = conf_str_skip_space(value + 1);
    } else if(*value != '\0') {
      error_exit("Invalid separator in comma-list: \'%c\' (value 0x%02X index %d)\n", *value, *value, index);
    }
    index++;
  }
  // Could not allocate 0 element
  if(*count == 0) error_exit("At least one element must be present in the comma-list (line %d)\n", node->line);
  *list = (uint64_t *)malloc(*count * sizeof(uint64_t));
  SYSEXPECT(*list != NULL);
  // Then use a simpler loop to actually acquire the numbers
  value = conf_str_skip_space(node->value);
  index = 0;
  while(*value != '\0') {
    char *end;
    // Put the parsed number into the list
    (*list)[index++] = strtoul(value, &end, 0);
    value = conf_str_skip_space(end);
    // We have already checked it will either be '\0' or ','
    if(*value == ',') value = conf_str_skip_space(value + 1);
  }
  assert(index == *count);
  return 1;
}

void conf_print(conf_t *conf) {
  conf_node_t *curr = conf->head;
  while(curr) {
    printf("Line %d: %s = %s\n", curr->line, curr->key, curr->value);
    curr = curr->next;
  }
}

int conf_selfcheck(conf_t *conf) {
  int count = 0;
  conf_node_t *node = conf->head;
  while(node) {
    count++;
    if(node->key == NULL || node->value == NULL) return 0;
    node = node->next;
  }
  if(count != conf->item_count) return 0;
  return 1;
}

// Prints out unaccessed nodes
void conf_print_unused(conf_t *conf) {
  if(conf->warn_unused == 0) return; // Do not print if this is turned off (default off)
  conf_node_t *curr = conf->head;
  while(curr) {
    if(curr->accessed == 0) printf("WARNING: Unused option \"%s\" = \"%s\"\n", curr->key, curr->value);
    curr = curr->next;
  }
}

void conf_conf_print(conf_t *conf) {
  printf("---------- conf_t ----------\n");
  printf("File name: %s; nodes %d\n", conf->filename, conf->item_count);
  conf_print_unused(conf);
  return;
}

void conf_dump(conf_t *conf, const char *filename) {
  FILE *fp = fopen(filename, "w");
  SYSEXPECT(fp != NULL);
  conf_node_t *node = conf->head;
  while(node) {
    fprintf(fp, "%s = %s\n\n", node->key, node->value);
    node = node->next;
  }
  fclose(fp);
  return;
}

void conf_begin_prefix(conf_t *conf, conf_it_t *it, const char *prefix) {
  int len = strlen(prefix);
  *it = conf->head;
  while(*it != NULL) {
    if(strncmp((*it)->key, prefix, len) == 0) {
      break;
    }
    *it = (*it)->next;
  }
  return;
}

void conf_next_prefix(conf_t *conf, conf_it_t *it, const char *prefix) {
  int len = strlen(prefix);
  // This is before comparison otherwise we will not make progress
  *it = (*it)->next;
  while(*it != NULL) {
    if(strncmp((*it)->key, prefix, len) == 0) {
      break;
    }
    *it = (*it)->next;
  }
  (void)conf;
  return;
}

//* tracer_t

const char *tracer_mode_names[2] = {
  "MODE_WRITE", "MODE_READ",
};

const char *tracer_cap_mode_names[5] = {
  "CAP_MODE_NONE", "CAP_MODE_INST", "CAP_MODE_LOAD", 
  "CAP_MODE_STORE", "CAP_MODE_MEMOP",
};

const char *tracer_record_type_names[7] = {
  "LOAD", "STORE", "L1-EVICT", "L2-EVICT", "L3-EVICT", "INST", "CYCLE",
};

const char *tracer_cleanup_names[2] = {
  "KEEP", "REMOVE",
};

const char *tracer_core_status_names[2] = {
  "ACTIVE", "HALTED",
};

// Both functions do not append new line after the print
void tracer_record_print(tracer_record_t *rec) {
  printf("type %s (%d) id %d addr 0x%lX cycle %lu serial %lu",
    tracer_record_type_names[rec->type], rec->type, rec->id, rec->line_addr, rec->cycle, rec->serial);
}

void tracer_record_print_buf(tracer_record_t *rec, char *buffer, int size) {
  snprintf(buffer, size, "type %s (%d) id %d addr 0x%lX cycle %lu serial %lu",
    tracer_record_type_names[rec->type], rec->type, rec->id, rec->line_addr, rec->cycle, rec->serial);
}

// mode is passed from the tracer init function
//   For read mode, we initialize file size and record count var for the core
//   For write mode, file size and all counters are initialized to zero
tracer_core_t *tracer_core_init(const char *basename, int id, int mode) {
  tracer_core_t *tracer_core = (tracer_core_t *)malloc(sizeof(tracer_core_t));
  SYSEXPECT(tracer_core != NULL);
  memset(tracer_core, 0x00, sizeof(tracer_core_t));
  tracer_core->id = id;
  tracer_core->status = TRACER_CORE_ACTIVE;
  // First generate file name
  int len = strlen(basename);
  // basename + "_" + [0 - 63] + '\0'
  tracer_core->filename = (char *)malloc(len + 1 + 2 + 1);
  strcpy(tracer_core->filename, basename);
  strcat(tracer_core->filename, "_");
  char num_buf[4];
  sprintf(num_buf, "%d", id);
  assert(strlen(num_buf) < 3);
  strcat(tracer_core->filename, num_buf);
  assert(strlen(tracer_core->filename) <= len + 1UL + 2UL);
  // Next open the file - do not assume the pos of the file ptr
  if(mode == TRACER_MODE_WRITE) {
    // wb+ allows us to both read and write, and will not truncate the file on fopen()
    tracer_core->fp = fopen(tracer_core->filename, "wb+"); 
  } else if(mode == TRACER_MODE_READ) {
    // Only read with "rb"
    tracer_core->fp = fopen(tracer_core->filename, "rb");
  } else {
    error_exit("Unknown tracer mode: %d\n", mode);
  }
  if(tracer_core->fp == NULL) {
    printf("Cannot open file: \"%s\"; Reasons below\n", tracer_core->filename);
    SYSEXPECT(tracer_core->fp != NULL);
  }
  // Get file size if read mode
  if(mode == TRACER_MODE_READ) {
    fseek(tracer_core->fp, 0, SEEK_END);
    tracer_core->filesize = ftell(tracer_core->fp);
    fseek(tracer_core->fp, 0, SEEK_SET);
    if(tracer_core->filesize % sizeof(tracer_record_t) != 0) {
      error_exit("File size %lu (%s) is not a multiple of record size!\n", 
        tracer_core->filesize, tracer_core->filename);
    }
    tracer_core->record_count = tracer_core->filesize / sizeof(tracer_record_t);
    tracer_core_begin(tracer_core);
  } else {
    tracer_core->filesize = 0UL;
  }
  return tracer_core;
}

void tracer_core_free(tracer_core_t *tracer_core, int do_flush) {
  if(do_flush) tracer_core_flush(tracer_core);
  fclose(tracer_core->fp);
  free(tracer_core->filename);
  free(tracer_core);
  return;
}

void tracer_core_flush(tracer_core_t *core) {
  assert(core->write_index >= 0 && core->write_index <= TRACER_BUFFER_SIZE);
  if(core->write_index == 0) return;
  uint64_t flush_size = core->write_index * sizeof(tracer_record_t);
  int write_ret = fwrite(core->buffer, flush_size, 1, core->fp);
  core->fwrite_count++;
  if(write_ret != 1) {
    error_exit("fwrite() returns %d (expect %d)\n", write_ret, 1);
  }
  core->filesize += flush_size;
  core->write_index = 0;
  return;
}

// Fill the buffer from current index and fp; Return the number of records filled
// We always read in the next batch from index 0, overwriting the current location
int tracer_core_fill(tracer_core_t *core) {
  assert(core->record_count >= core->read_count);
  assert(core->read_index == core->max_index);
  uint64_t remain_count = core->record_count - core->read_count;
  // Read count is the smaller one
  int reads = remain_count > TRACER_BUFFER_SIZE ? TRACER_BUFFER_SIZE : remain_count;
  int ret = fread(core->buffer, sizeof(tracer_record_t), reads, core->fp);
  core->fread_count++;
  if(ret != reads) {
    error_exit("fread() returns %d (expect %d)\n", ret, reads);
  }
  core->read_index = 0;           // Points to the next record
  core->read_count += reads; // This controls future refills
  core->max_index = reads;   // This controls future reads
  assert(core->read_count <= core->record_count);
  return reads;
}

// This is called before starting read iteration. We call it in init function also.
void tracer_core_begin(tracer_core_t *core) {
  tracer_core_flush(core); // Avoid missing the last few records
  core->read_index = core->max_index = 0; // This will trigger fill on first read
  core->read_count = 0UL;
  fseek(core->fp, 0, SEEK_SET);
  // The next three will be updated
  core->load_count = 0UL;
  core->store_count = 0UL;
  core->memop_count = 0UL;
  core->l1_evict_count = 0UL;
  core->l2_evict_count = 0UL;
  core->l3_evict_count = 0UL;
  return;
}

// This impl. is used for both next() and peek() call
// If inc_index is 1, we increment the index. Otherwise we do not
tracer_record_t *_tracer_core_next(tracer_core_t *core, int inc_index) {
  assert(inc_index == 0 || inc_index == 1);
  if(tracer_core_is_end(core)) return NULL;
  // Read next batch into the buffer
  if(core->read_index == core->max_index) {
    tracer_core_fill(core);
    core->read_index = 0;
  }
  tracer_record_t *ret = core->buffer + core->read_index;
  core->read_index += inc_index; // This may cause index to be invalid
  assert(core->read_index >= 0 && core->read_index <= core->max_index);
  return ret;
}

// If the file name exists then remove them
void tracer_core_remove_file(tracer_core_t *tracer_core) {
  if(tracer_core->filename == NULL) return;
  printf("Deleting file: %s\n", tracer_core->filename);
  int ret = remove(tracer_core->filename);
  // Do not exit; just print error if removal fails
  if(ret != 0) printf("WARNING: Failed to remove file \"%s\"\n", tracer_core->filename);
  return;
}

// Mode can be either write or read
tracer_t *tracer_init(const char *basename, int core_count, int mode) {
  if(core_count > TRACER_MAX_CORE) {
    error_exit("Only support %d cores max (see %d)\n", TRACER_MAX_CORE, core_count);
  }
  tracer_t *tracer = (tracer_t *)malloc(sizeof(tracer_t));
  SYSEXPECT(tracer != NULL);
  memset(tracer, 0x00, sizeof(tracer_t));
  tracer->core_count = core_count;
  tracer->active_core_count = core_count;
  // Allocate memory for base name
  tracer->basename = (char *)malloc(strlen(basename) + 1);
  SYSEXPECT(tracer->basename != NULL);
  strcpy(tracer->basename, basename);
  tracer->cores = (tracer_core_t **)malloc(sizeof(tracer_core_t *) * core_count);
  SYSEXPECT(tracer->cores != NULL);
  for(int i = 0;i < core_count;i++) {
    tracer->cores[i] = tracer_core_init(basename, i, mode);
  }
  assert(mode == TRACER_MODE_READ || mode == TRACER_MODE_WRITE);
  tracer->mode = mode;
  // The following two are set via an extra function call since they are not essential
  tracer->cap = 0UL;
  tracer->cap_mode = TRACER_CAP_MODE_NONE;
  return tracer;
}

void tracer_free(tracer_t *tracer) {
  for(int i = 0;i < tracer->core_count;i++) {
    if(tracer->cleanup) tracer_core_remove_file(tracer->cores[i]);
    // Only flush the core on write mode
    tracer_core_free(tracer->cores[i], tracer->mode == TRACER_MODE_WRITE);
  }
  free(tracer->cores);
  free(tracer->basename);
  free(tracer);
  return;
}

void tracer_set_cap_mode(tracer_t *tracer, int cap_mode, uint64_t cap) {
  if(cap_mode < TRACER_CAP_MODE_BEGIN || cap_mode >= TRACER_CAP_MODE_END) {
    error_exit("Unknown cap mode: %d\n", cap_mode);
  } else if(cap_mode != TRACER_CAP_MODE_LOAD && 
            cap_mode != TRACER_CAP_MODE_STORE && cap_mode != TRACER_CAP_MODE_MEMOP) {
    error_exit("Currently only LOAD/STORE/MEMOP is supported for cap_mode\n");
  }
  tracer->cap_mode = cap_mode;
  tracer->cap = cap;
  return;
} 

void tracer_set_cleanup(tracer_t *tracer, int value) {
  assert(value == TRACER_KEEP_FILE || value == TRACER_REMOVE_FILE);
  tracer->cleanup = value;
  return;
}

// Note: Cores calling this function concurrently may call flush() concurrently.
// This will not have race condition, since we write to different files
void tracer_insert(tracer_t *tracer, int type, int id, uint64_t line_addr, uint64_t cycle, uint64_t serial) {
  if(unlikely(tracer->mode != TRACER_MODE_WRITE)) {
    error_exit("Insert can only be called under write mode (see %d)\n", tracer->mode);
  }
  assert(id < tracer->core_count);
  tracer_core_t *core = tracer->cores[id];
  // Do not add tracer to the core
  if(core->status == TRACER_CORE_HALTED) return;
  // Dump to the file and reset the index
  if(core->write_index == TRACER_BUFFER_SIZE) {
    tracer_core_flush(core);
    assert(core->write_index == 0);
  }
  assert(core->write_index < TRACER_BUFFER_SIZE);
  int index = core->write_index;
  core->buffer[index].type = type;
  core->buffer[index].id = id;
  core->buffer[index].line_addr = line_addr;
  core->buffer[index].cycle = cycle;
  core->buffer[index].serial = serial;
  core->write_index++;
  core->record_count++;
  switch(type) {
    case TRACER_LOAD: {
      core->load_count++;
      core->memop_count++;
    } break;
    case TRACER_STORE: {
      core->store_count++;
      core->memop_count++;
    } break;
    case TRACER_L1_EVICT: {
      core->l1_evict_count++;
    } break;
    case TRACER_L2_EVICT: {
      core->l2_evict_count++;
    } break;
    case TRACER_L3_EVICT: {
      core->l3_evict_count++;
    } break;
    default: break; // There can be other types of records
  }
  if((tracer->cap_mode == TRACER_CAP_MODE_LOAD && core->load_count == tracer->cap) || 
     (tracer->cap_mode == TRACER_CAP_MODE_STORE && core->store_count == tracer->cap) || 
     (tracer->cap_mode == TRACER_CAP_MODE_MEMOP && core->memop_count == tracer->cap)) {
    core->status = TRACER_CORE_HALTED;
    tracer_core_flush(core); // Flush all buffered records
    assert(tracer->active_core_count > 0);
    tracer->active_core_count--;
    if(tracer->active_core_count == 0) {
      printf("*** Finished recording (cap_mode %s cap %lu).\n", 
        tracer_cap_mode_names[tracer->cap_mode], tracer->cap);
      printf("*** Configuration\n");
      tracer_conf_print(tracer);
      printf("*** Statistics\n");
      tracer_stat_print(tracer, 1); // Always verbose
#ifdef UTIL_TEST
      printf("*** tracer->cap_debug != 0 resume normal execution!\n");
#else
      tracer_free(tracer);
      printf("*** Freed tracer object. Exiting now.\n");
      exit(0);
#endif
    }
  }
  return;
}

// Only valid in read mode; Report error if begin is out of range; Do not report error if begin + count is
// This function can also be called at write mode; We will change the pointer to the end of file after this
void tracer_print(tracer_t *tracer, int id, uint64_t begin, uint64_t count) {
  if(id < 0 || id >= tracer->core_count) {
    error_exit("Core ID is out of the range: 0 - %d (see %d))\n", tracer->core_count - 1, id);
  }
  tracer_core_t *core = tracer->cores[id];
  // Make sure file size is updated and file content is synced
  tracer_core_flush(core);
  if(begin >= core->record_count) {
    error_exit("Arg begin is larger than the trace size %lu (see %lu)\n", core->record_count, begin);
  }
  uint64_t offset = begin * sizeof(tracer_record_t);
  fseek(core->fp, offset, SEEK_SET);
  tracer_record_t record;
  for(uint64_t i = 0;i < count;i++) {
    assert(begin + i <= core->record_count);
    // Do not print over the file end
    if(begin + i == core->record_count) break;
    int read_ret = fread(&record, sizeof(record), 1, core->fp);
    core->fread_count++;
    if(read_ret != 1) {
      error_exit("fread() returns %d (expect %d)\n", read_ret, 1);
    }
    if(record.type < TRACE_TYPE_BEGIN || record.type >= TRACER_TYPE_END) {
      error_exit("Invalid record type: %d\n", record.type);
    }
    // We only print serial number here
    printf("Rec #%lu: ", begin + i);
    tracer_record_print(&record);
    putchar('\n');
  }
  // Move to end in case it is a write
  fseek(core->fp, 0, SEEK_END);
  return;
} 

void tracer_begin(tracer_t *tracer) {
  for(int i = 0;i < tracer->core_count;i++) {
    tracer_core_begin(tracer->cores[i]);
  }
  return;
}

// Selects a record with min cycle and return
tracer_record_t *tracer_next(tracer_t *tracer) {
  uint64_t min_serial = -1UL;
  tracer_record_t *min_rec = NULL;
  tracer_core_t *min_core = NULL;
  for(int i = 0;i < tracer->core_count;i++) {
    tracer_core_t *core = tracer->cores[i];
    tracer_record_t *rec = tracer_core_peek(core);
    if(rec == NULL) {
      continue;
    } else if(rec->serial < min_serial) {
      min_serial = rec->serial;
      min_rec = rec;
      min_core = core;
    }
  }
  if(min_core != NULL) tracer_core_next(min_core);
  // Count loads and stores
  if(min_core != NULL) {
    assert(min_rec != NULL);
    switch(min_rec->type) {
      case TRACER_LOAD: {
        min_core->load_count++; 
        min_core->memop_count++;
        break;
      } case TRACER_STORE: {
        min_core->store_count++; 
        min_core->memop_count++;
        break;
      } case TRACER_L1_EVICT: {
        min_core->l1_evict_count++;
        break;
      } case TRACER_L2_EVICT: {
        min_core->l2_evict_count++;
        break;
      } case TRACER_L3_EVICT: {
        min_core->l3_evict_count++;
        break;
      } default: {
        break;
      }
    }
  }
  return min_rec; // Could be NULL meaning all cores are done
}

uint64_t tracer_get_record_count(tracer_t *tracer) {
  uint64_t ret = 0UL;
  for(int i = 0;i < tracer->core_count;i++) {
    tracer_core_t *core = tracer->cores[i];
    ret += core->record_count;
  }
  return ret;
}

uint64_t tracer_get_core_record_count(tracer_t *tracer, int id) {
  if(id < 0 || id >= tracer->core_count) {
    error_exit("Core ID is out of the range: 0 - %d (see %d))\n", tracer->core_count - 1, id);
  }
  tracer_core_t *core = tracer->cores[id];
  return core->record_count;
}

void tracer_conf_print(tracer_t *tracer) {
  printf("---------- tracer_t ----------\n");
  printf("Base name \"%s\" buf %d mode %s cap_mode %s cap %lu cleanup %s\n", 
    tracer->basename, TRACER_BUFFER_SIZE, tracer_mode_names[tracer->mode], tracer_cap_mode_names[tracer->cap_mode], tracer->cap,
    tracer_cleanup_names[tracer->cleanup]);
  return;
}

void tracer_stat_print(tracer_t *tracer, int verbose) {
  printf("---------- tracer_t ----------\n");
  uint64_t total_load = 0, total_store = 0, total_inst = 0, total_memop = 0;
  uint64_t total_record = 0, total_read = 0, total_fread = 0, total_fwrite = 0;
  uint64_t total_l1_evict = 0, total_l2_evict = 0, total_l3_evict = 0;
  for(int i = 0;i < tracer->core_count;i++) {
    tracer_core_t *core = tracer->cores[i];
    tracer_core_flush(core);
    if(verbose) {
      printf("Core %d: load %lu store %lu memop %lu inst %lu Evict L1 %lu L2 %lu L3 %lu\n",
        core->id, core->load_count, core->store_count, core->memop_count, core->inst_count,
        core->l1_evict_count, core->l2_evict_count, core->l3_evict_count);
      printf("       %srecord %lu reads %lu status %s\n", core->id >= 10 ? "  " : " ",
        core->record_count, core->read_count, tracer_core_status_names[core->status]);
      printf("       %sfread %lu fwrite %lu sz %lu\n", core->id >= 10 ? "  " : " ",
        core->fread_count, core->fwrite_count, core->filesize);
    }
    total_load += core->load_count;
    total_store += core->store_count;
    total_inst += core->inst_count;
    total_memop += core->memop_count;
    total_record += core->record_count;
    total_l1_evict += core->l1_evict_count;
    total_l2_evict += core->l2_evict_count;
    total_l3_evict += core->l3_evict_count;
    total_read += core->read_count;
    total_fread += core->fread_count;
    total_fwrite += core->fwrite_count;
  }
  printf("Total: load %lu store %lu memop %lu inst %lu Evict L1 %lu L2 %lu L3 %lu\n",
    total_load, total_store, total_memop, total_inst, total_l1_evict, total_l2_evict, total_l3_evict);
  printf("       reocrd %lu reads %lu fread %lu fwrite %lu\n", 
    total_record, total_read, total_fread, total_fwrite);
  return;
}

