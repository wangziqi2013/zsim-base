
#include "util.h"

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

//* conf_t

// There is no guarantee that k and v are zero term string
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

conf_t *conf_init(const char *filename) {
  conf_t *conf = (conf_t *)malloc(sizeof(conf_t));
  SYSEXPECT(conf != NULL);
  memset(conf, 0x00, sizeof(conf_t));
  // Passing NULL as the second argument forces the lib call to allocate the buffer using malloc()
  conf->filename = realpath(filename, NULL);
  SYSEXPECT(conf->filename != NULL);
  char buf[1024];
  FILE *fp = fopen(filename, "r");
  SYSEXPECT(fp != NULL);
  int curr_line = 0;
  // Get file size and reset fp
  fseek(fp, 0, SEEK_END);
  int sz = ftell(fp);
  fseek(fp, 0, SEEK_SET);
  // Note: We used to use feof() here. But feof() only returns true when a non-existing offset is actually read, which 
  // is not the case for fgets, since this function returns as long as it sees a '\n', which does not read past the EOF
  while(ftell(fp) < sz) {
    curr_line++;
    //printf("%d\n", curr_line);
    char *fgets_ret = fgets(buf, 1024, fp);   // This will add a '\0' automatically and also append '\n' if there is one
    (void)fgets_ret; // Required by stricter rule of some compilers
    //printf("Read line \"%s\"\n", buf);
    int len = strlen(buf);
    assert(len < 1024);
    if(len > 0 && buf[len - 1] != '\n' && !feof(fp)) // Check for lines that do not end with '\n' within first 1023 bytes
      error_exit("Line %d too long (> 1024 bytes)\n", curr_line);
    int empty_line = 1;               // Whether the line content is all space character
    for(int i = 0;i < len;i++) {
      if(empty_line && buf[i] == '#') {
        break;                        // Skip the line if begins with '#'
      } else if(!isspace(buf[i])) { 
        empty_line = 0;
        break;
      }
    }
    if(empty_line) continue;
    int assign_index = -1;
    for(int i = 0;i < len;i++) if(buf[i] == '=') assign_index = i;
    if(assign_index == -1) error_exit("Did not find \"=\" sign on line %d\n", curr_line);
    int key_begin = 0, key_end = assign_index - 1;
    int value_begin = assign_index + 1, value_end = len - 1;
    assert(key_begin <= key_end);
    assert(value_begin <= value_end);
    while(key_begin < key_end && (isspace(buf[key_begin]) || isspace(buf[key_end]))) {
      if(isspace(buf[key_begin])) key_begin++;
      if(isspace(buf[key_end])) key_end--;
    }
    if(key_begin == key_end && isspace(buf[key_begin])) 
      error_exit("Empty key on line %d\n", curr_line);
    // Special processing: Remove everything in the value after '#'
    for(int i = value_begin;i < value_end;i++) {
      if(buf[i] == '#') {
        if(i == value_begin) error_exit("Comment must not occur after \'=\'\n");
        value_end = i - 1;
        // Enable the following to print the string between value begin and end
        /*char t = buf[value_end + 1]; 
        buf[value_end + 1] = '\0';
        puts(buf + value_begin);
        buf[value_end + 1] = t;*/
        break;
      }
    }
    while(value_begin < value_end && (isspace(buf[value_begin]) || isspace(buf[value_end]))) {
      if(isspace(buf[value_begin])) value_begin++;
      if(isspace(buf[value_end])) value_end--;
    }
    if(value_begin == value_end && isspace(buf[value_begin])) { // Crossed and the current value is space
      error_exit("Empty value on line %d\n", curr_line);
    } else if(buf[value_begin] == '\"' || buf[value_begin] == '\'' || 
              buf[value_end] == '\"' || buf[value_end] == '\'') {
      error_exit("Value should not start or end with \"\'\" or \"\"\" (line %d)\n", curr_line);
    }
    
    int key_len = key_end - key_begin + 1;
    int value_len = value_end - value_begin + 1;
    conf_insert(conf, buf + key_begin, buf + value_begin, key_len, value_len, curr_line);
  }
  fclose(fp);
  return conf;
}

void conf_node_free(conf_node_t *node) {
  free(node->key);
  free(node->value);
  free(node);
  return;
}

void conf_free(conf_t *conf) {
  conf_node_t *curr = conf->head;
  while(curr) {
    conf_node_t *prev = curr;
    curr = curr->next;
    conf_node_free(prev);
  }
  free(conf->filename);
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
  free(node->value); // Recall that this is allocated using malloc()
  char *new_val = (char *)malloc(strlen(value) + 1);
  SYSEXPECT(new_val != NULL);
  strcpy(new_val, value);
  node->value = new_val;
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
  } else {
    error_exit("Could not recognize abbreviation \"%s\" after numeric value \"%lu\"\n", end, t);
  }
  return 1;
}

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
// 2. Value must be in the given range, if CONF_RANGE is on
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

//* tracer_t

const char *tracer_mode_names[2] = {
  "MODE_WRITE", "MODE_READ",
};

const char *tracer_cap_mode_names[5] = {
  "CAP_MODE_NONE", "CAP_MODE_INST", "CAP_MODE_LOAD", 
  "CAP_MODE_STORE", "CAP_MODE_MEMOP",
};

const char *tracer_record_type_names[5] = {
  "LOAD", "STORE", "L1-EVICT", "L2-EVICT", "L3-EVICT",
};

const char *tracer_cleanup_names[2] = {
  "KEEP", "REMOVE",
};

const char *tracer_core_status_names[2] = {
  "ACTIVE", "HALTED",
};

// Both functions do not append new line after the print
void tracer_record_print(tracer_record_t *rec) {
  printf("type %s (%d) id %d addr 0x%lX cycle %lu",
    tracer_record_type_names[rec->type], rec->type, rec->id, rec->line_addr, rec->cycle);
}

void tracer_record_print_buf(tracer_record_t *rec, char *buffer, int size) {
  snprintf(buffer, size, "type %s (%d) id %d addr 0x%lX cycle %lu",
    tracer_record_type_names[rec->type], rec->type, rec->id, rec->line_addr, rec->cycle);
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
void tracer_insert(tracer_t *tracer, int type, int id, uint64_t line_addr, uint64_t cycle) {
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
    default: break;
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
      tracer_stat_print(tracer);
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
  uint64_t min_cycle = -1UL;
  tracer_record_t *min_rec = NULL;
  int min_index = -1;
  for(int i = 0;i < tracer->core_count;i++) {
    tracer_core_t *core = tracer->cores[i];
    tracer_record_t *rec = tracer_core_peek(core);
    if(rec == NULL) {
      continue;
    } else if(rec->cycle < min_cycle) {
      min_cycle = rec->cycle;
      min_rec = rec;
      min_index = i;
    }
  }
  if(min_index != -1) tracer_core_next(tracer->cores[min_index]);
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

void tracer_stat_print(tracer_t *tracer) {
  printf("---------- tracer_t ----------\n");
  uint64_t total_load = 0, total_store = 0, total_inst = 0, total_memop = 0;
  uint64_t total_record = 0, total_read = 0, total_fread = 0, total_fwrite = 0;
  for(int i = 0;i < tracer->core_count;i++) {
    tracer_core_t *core = tracer->cores[i];
    tracer_core_flush(core);
    printf("Core %d: load %lu store %lu inst %lu memop %lu record %lu reads %lu status %s\n",
      core->id, core->load_count, core->store_count, core->inst_count, core->memop_count, core->record_count,
      core->read_count, tracer_core_status_names[core->status]);
    printf("  fread %lu fwrite %lu\n", core->fread_count, core->fwrite_count);
    total_load += core->load_count;
    total_store += core->store_count;
    total_inst += core->inst_count;
    total_memop += core->memop_count;
    total_record += core->record_count;
    total_read += core->read_count;
    total_fread += core->fread_count;
    total_fwrite += core->fwrite_count;
    if(tracer->mode == TRACER_MODE_READ) {
      printf("    file size %lu\n", core->filesize);
    }
  }
  printf("Total: load %lu store %lu inst %lu memop %lu reocrd %lu reads %lu\n",
    total_load, total_store, total_inst, total_memop, total_record, total_read);
  printf("  fread %lu fwrite %lu\n", total_fread, total_fwrite);
  return;
}

