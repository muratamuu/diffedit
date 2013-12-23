/*!
 * diff output formatter
 * (author murata.muu@gmail.com)
 *
 * 20110704 1.0 new release
 * 20110730 1.1 expandTAB bug fix
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <errno.h>
#include <unistd.h>
#include <exception>
#include <list>
#include <string>

#define VERSION "1.1"

#define LINEBUFSIZE (256)
#define FILENAMESIZE (128)
#define TABSIZE (4)
#define DEFAULT_COLUM (80)
#define DEFAULT_READ_CACHE_SIZE (30)
#define MODE_EQL ' '
#define MODE_ADD 'A'
#define MODE_MOD 'M'
#define MODE_DEL 'D'

#define ENCODING_UNKNOWN (0x00)
#define ENCODING_EUC     (0x01)
#define ENCODING_SJIS    (0x02)
#define ENCODING_UTF8    (0x04)

int strcolumlen(char* in);
void cutLF(char* buf)
{
  char* p;
  if (p = strchr(buf, '\r')) *p = 0;
  if (p = strchr(buf, '\n')) *p = 0;
}

void expandTAB(char* buf)
{
  char tmp[LINEBUFSIZE];
  memset(tmp, 0, sizeof(tmp));
  char* src = buf;
  char* dst = tmp;

  while(*src) {
    if (*src == '\t') {
      int n_sp = TABSIZE - (strcolumlen(tmp) % TABSIZE); // 20110730 added
      memset(dst, ' ', n_sp);
      dst += n_sp;
    } else {
      *dst = *src;
      dst++;
    }
    src++;
  }
  strcpy(buf, tmp);
}

void trimspace(char* buf)
{
  char* sp = buf;
  char* ep = buf + strlen(buf)-1;
  while(*sp) {
    if (*sp != ' ' && *sp != '\t') break;
    sp++;
  }
  while(sp != ep) {
    if (*ep != ' ' && *ep != '\t') break;
    ep--;
  }
  ep++;
  while(sp != ep)
    *buf++ = *sp++;
  *buf = 0;
}

bool is_ascii(unsigned char c)
{
  if (0x00 <= c && c <= 0x7F)
    return true;
  return false;
}

bool is_sjis_hankana(unsigned char c)
{
  if (0xA1 <= c && c <= 0xDF)
    return true;
  return false;
}

bool is_sjis_zenkaku(unsigned char* c)
{
  if ((0x81 <= *c && *c <= 0x9F) || (0xE0 <= *c && *c <= 0xFC))
    if ((0x40 <= *(c+1) && *(c+1) <= 0x7E) || (0x80 <= *(c+1) && *(c+1) <= 0xFC))
      return true;
  return false;
}

bool is_euc_hankana(unsigned char* c)
{
  if (*c == 0x8E)
    if (0xA1 <= *(c+1) && *(c+1) <= 0xDF)
      return true;
  return false;
}

bool is_euc_zenkaku(unsigned char* c)
{
  if (0xA1 <= *c || *c <= 0xFE)
    if (0xA1 <= *c || *c <= 0xFE)
      return true;
  return false;
}

bool is_utf8_2byte(unsigned char* c)
{
  if (0xC2 <= *c && *c <= 0xDF)
    if (0x80 <= *(c+1) && *(c+1) <= 0xBF)
      return true;
  return false;
}

bool is_utf8_3byte(unsigned char* c)
{
  if (0xE0 <= *c && *c <= 0xEF)
    if (0x80 <= *(c+1) && *(c+1) <= 0xBF)
      if (0x80 <= *(c+2) && *(c+2) <= 0xBF)
        return true;
  return false;
}

// 20110730 added
int strcolumlen(char* str)
{
  int col = 0;
  int max = strlen(str);
  unsigned char* in = (unsigned char*)str;
  for (int i = 0; i < max;) {
    if (is_ascii(in[i])) {
      i += 1;
      col += 1;
    } else if (is_utf8_3byte(&in[i])) {
      i += 3;
      col += 2;
    } else if (is_utf8_2byte(&in[i])  ||
               is_euc_zenkaku(&in[i]) ||
               is_sjis_zenkaku(&in[i])) {
      i += 2;
      col += 2;
    } else if (is_euc_hankana(&in[i])) {
      i += 2;
      col += 1;
    } else if (is_sjis_hankana(in[i])) {
      i += 1;
      col += 1;
    } else {
      i += 1;
      col += 1;
    }
  }
  return col;
}

#define THROW_EXCEPTION(format, args...)                        \
  {                                                             \
    AppException e;                                             \
    e.SetMessageV("%s:%d " format, __FILE__, __LINE__, ##args); \
    throw e;                                                    \
  }

class AppException : public std::exception
{
public:
  AppException() {}
  ~AppException() throw() {}
  void SetMessageV(const char* format, ...) {
    va_list va;
    va_start(va, format);
    vsnprintf(message_, sizeof(message_), format, va);
    va_end(va);
  }
  const char* what() { return message_; }
private:
  char message_[128];
};

class Reader
{
public:
  Reader(FILE* fp = stdin, int cachesize = DEFAULT_READ_CACHE_SIZE)
    : fp_(fp), isSelfOpened_(false), cachesize_(cachesize), c_pos_(-1) {
    init();
  }
  Reader(const char* filename, int cachesize = DEFAULT_READ_CACHE_SIZE)
    : isSelfOpened_(true), cachesize_(cachesize), c_pos_(-1) {
    if (!(fp_ = fopen(filename, "r")))
      THROW_EXCEPTION("fopen(%s) %s", filename, strerror(errno));
    init();
  }
  ~Reader() {
    clear_cache();
    if (fp_ && isSelfOpened_) fclose(fp_);
  }
  char* readline();
  void reset() { c_pos_ = -1; }
  void forward();
  void rewind();
  char* prev() { return getstr(c_pos_ - 1); }
  char* crnt() { return getstr(c_pos_); }
  char* next() { return getstr(c_pos_ + 1); }
  int cachesize() { return cachesize_; }

private:
  void clear_cache();
  void init();
  char* getstr(int pos);
  char* realread();
  char* normalize(const char* line);
  FILE* fp_;
  bool isSelfOpened_;
  int cachesize_;
  int c_pos_;
  std::list<std::string*> cache_;
};

void Reader::init()
{
  for (int i = 0; i < cachesize_ + 1; i++) {
    char* line = realread();
    if (line)
      cache_.push_back(new std::string(line));
    else {
      if (i == 0) THROW_EXCEPTION("initialize  error");
      cachesize_ = i - 1;
      break;
    }
  }
  c_pos_ = -1;
}

char* Reader::getstr(int pos)
{
  static char line[LINEBUFSIZE];

  if (pos < 0)
    return NULL;

  int i = 0;
  for (std::list<std::string*>::iterator it = cache_.begin();
       it != cache_.end(); it++, i++) {
    if (i == pos)
      return normalize((*it)->c_str());
  }
  return NULL;
}

char* Reader::readline()
{
  forward();
  return crnt();
}

void Reader::clear_cache()
{
  for (std::list<std::string*>::iterator it = cache_.begin();
       it != cache_.end(); it++)
    delete *it;
  cache_.clear();
}

char* Reader::realread()
{
  static char line[LINEBUFSIZE];
  if (!fgets(line, sizeof(line)-1, fp_))
    return NULL;
  return line;
}

void Reader::forward()
{
  if (c_pos_ == cachesize_ - 1) {
    delete cache_.front();
    cache_.pop_front();
    char* line = realread();
    if (line)
      cache_.push_back(new std::string(line));
  } else {
    c_pos_++;
  }
  return;
}

void Reader::rewind()
{
  if (c_pos_ == 0) THROW_EXCEPTION("rewind error");
  c_pos_--;
}

char* Reader::normalize(const char* buf)
{
  static char line[LINEBUFSIZE];
  strncpy(line, buf, sizeof(line)-1);
  cutLF(line);
  expandTAB(line);
  return line;
}

class Line
{
public:
  Line() : start_(0), end_(0) {}
  Line(int start, int end) : start_(start), end_(end) {}
  ~Line() {
    for (std::list<std::string*>::iterator it = strs_.begin();
         it != strs_.end(); it++)
      delete *it;
  }
  void set_start(int start) { start_ = start; }
  void set_end(int end) { end_ = end; }
  int start() { return start_; }
  int end() { return end_; }
  void addstr(char* str) {
    strs_.push_back(new std::string(str));
  }
  void debug() {
    fprintf(stderr, "start[%5d] end[%5d]\n", start_, end_);
    for (std::list<std::string*>::iterator it = strs_.begin();
         it != strs_.end(); it++)
      fprintf(stderr, "[%s]\n", (*it)->c_str());
  }
  std::string* getstr() {
    if (!strs_.empty()) {
      std::string* p = strs_.front();
      strs_.pop_front();
      return p;
    }
    return NULL;
  }
private:
  int start_;
  int end_;
  std::list<std::string*> strs_;
};

class Diff
{
public:
  Diff(Line* src, Line* dst, int mode) : src_(src), dst_(dst), mode_(mode) {}
  ~Diff() {
    if (src_) delete src_;
    if (dst_) delete dst_;
  }
  Line* src() { return src_; }
  Line* dst() { return dst_; }
  int mode() { return mode_; }
  void debug() {
    fprintf(stderr, "[mode] %c\n", mode_);
    if (src_) { fprintf(stderr, "[SRC] "); src_->debug(); }
    else      { fprintf(stderr, "[SRC] null\n");          }
    if (dst_) { fprintf(stderr, "[DST] "); dst_->debug(); }
    else      { fprintf(stderr, "[DST] null\n");          }
  }
private:
  Line* src_;
  Line* dst_;
  int mode_;
};

class Analyzer
{
public:
  Analyzer(Reader* reader) : reader_(reader) {reader_->reset();}
  ~Analyzer() { delete reader_; }
  static Analyzer* create(Reader* reader);
  const char* getsrc();
  virtual Diff* getdiff() = 0;
protected:
  char* parse_filename(char* line);
  Reader* reader_;
private:
  char* get_src_filename(char* filename);
};

class UnifiedAnalyzer : public Analyzer
{
public:
  UnifiedAnalyzer(Reader* reader) : Analyzer(reader) {}
  ~UnifiedAnalyzer() {}
  virtual Diff* getdiff();
private:
  char* parse_base_line(char* line, int* src_b, int* dst_b);
  bool is_ignore(char* line);
  bool is_diff_start();
  bool is_diff_boundary_src();
  bool is_diff_boundary_dst();
  bool is_diff_end();
};

class ContextAnalyzer : public Analyzer
{
public:
  ContextAnalyzer(Reader* reader) : Analyzer(reader) {}
  ~ContextAnalyzer() {}
  virtual Diff* getdiff();
private:
  char* parse_line_no(char* line, int* src_s, int* src_e,
                      int* dst_s, int* dst_e, int* mode);
  bool is_ignore(char* line);
};

Analyzer* Analyzer::create(Reader* reader)
{
  int unified, context;
  unified = context = 0;
  for (int i = 0; i < reader->cachesize(); i++) {
    char* line = reader->readline();
    switch (line[0]) {
    case '+':
    case '-':
    case '@':
      unified++;
      break;
    case '<':
    case '>':
      context++;
      break;
    }
  }
  reader->reset();
  if (unified > context) return new UnifiedAnalyzer(reader);
  else                   return new ContextAnalyzer(reader);
}

const char* Analyzer::getsrc()
{
  static char filename[FILENAMESIZE];
  memset(filename, 0, sizeof(filename));
  if (get_src_filename(filename))
    return filename;
  return NULL;
}

char* Analyzer::get_src_filename(char* filename)
{
  char* line = NULL;
  while(line = reader_->readline()) {
    if (char* p = parse_filename(line)) {
      strcpy(filename, p);
      trimspace(filename);
      break;
    }
  }
  return line;
}

char* Analyzer::parse_filename(char* line)
{
  if (!strncmp(line, "Index:", 6))
    return line+6;
  if (!strncmp(line, "diff ", 5))
    if (char* p = strrchr(line, '/'))
      return ++p;
  return NULL;
}

Diff* UnifiedAnalyzer::getdiff()
{
  static int src_b, src_c;
  static int dst_b, dst_c;
  int src_s, src_e;
  int dst_s, dst_e;
  int mode;

  Line* src = NULL;
  Line* dst = NULL;

  while (char* line = reader_->readline()) {
    if (parse_filename(line)) {
      reader_->rewind();
      break;
    }

    if (is_ignore(line))
      continue;

    if (parse_base_line(line, &src_b, &dst_b)) {
      src_c = dst_c = -1;
      continue;
    }

    if (reader_->crnt()[0] == ' ') {
      src_c++;
      dst_c++;
    } else if (reader_->crnt()[0] == '-') {
      if (!src) src = new Line();
      src->addstr(&reader_->crnt()[1]);
      src_c++;
    } else if (reader_->crnt()[0] == '+') {
      if (!dst) dst = new Line();
      dst->addstr(&reader_->crnt()[1]);
      dst_c++;
    }

    if (is_diff_start()) {
      src_s = src_e = dst_s = dst_e = 0;
      if (reader_->crnt()[0] == '-') {
        src_s = src_b + src_c;
        mode = MODE_DEL;
      } else {
        dst_s = dst_b + dst_c;
        mode = MODE_ADD;
      }
    }
    if (is_diff_boundary_src()) {
      src_e = src_b + src_c;
      mode = MODE_MOD;
    }
    if (is_diff_boundary_dst()) {
      dst_s = dst_b + dst_c;
      mode = MODE_MOD;
    }
    if (is_diff_end()) {
      if (reader_->crnt()[0] == '-')
        src_e = src_b + src_c;
      else
        dst_e = dst_b + dst_c;

      if (src) {
        src->set_start(src_s);
        src->set_end(src_e);
      }
      if (dst) {
        dst->set_start(dst_s);
        dst->set_end(dst_e);
      }
      Diff* diff = new Diff(src, dst, mode);
      src = dst = NULL;
      return diff;
    }
  }
  return NULL;
}

char* UnifiedAnalyzer::parse_base_line(char* line, int* src_b, int* dst_b)
{
  if (line[0] == '@' && line[1] == '@') {
    if (line = strchr(line, '-')) {
      line++;
      *src_b = atoi(line);
    }
    if (line = strchr(line, '+')) {
      line++;
      *dst_b = atoi(line);
    }
    return line;
  }
  return NULL;
}

bool UnifiedAnalyzer::is_ignore(char* line) {
  if (!strncmp(line, "---", 3)) return true;
  if (!strncmp(line, "+++", 3)) return true;
  if (line[0] == '=') return true;
  return false;
}

bool UnifiedAnalyzer::is_diff_start()
{
  if (reader_->prev()[0] != '-' && reader_->prev()[0] != '+')
    if (reader_->crnt()[0] == '-' || reader_->crnt()[0] == '+')
      return true;
  return false;
}

bool UnifiedAnalyzer::is_diff_boundary_src()
{
  if (reader_->crnt()[0] == '-' && reader_->next()[0] == '+')
    return true;
  return false;
}

bool UnifiedAnalyzer::is_diff_boundary_dst()
{
  if (reader_->prev()[0] == '-' && reader_->crnt()[0] == '+')
    return true;
  return false;
}

bool UnifiedAnalyzer::is_diff_end()
{
  if (reader_->crnt()[0] == '-' || reader_->crnt()[0] == '+')
    if (reader_->next()[0] != '-' && reader_->next()[0] != '+')
      return true;
  return false;
}

Diff* ContextAnalyzer::getdiff()
{
  int src_s, src_e, dst_s, dst_e;
  int mode, found_mode = 0;
  Line* src = NULL;
  Line* dst = NULL;

  while (char* line = reader_->readline()) {
    if (parse_filename(line)) {
      reader_->rewind();
      break;
    }

    if (is_ignore(line))
      continue;

    if (line[0] == '<') {
      src->addstr(&line[2]);
      continue;
    }
    if (line[0] == '>') {
      dst->addstr(&line[2]);
      continue;
    }

    if (parse_line_no(line, &src_s, &src_e, &dst_s, &dst_e, &mode)) {
      if (found_mode) {
        reader_->rewind();
        return new Diff(src, dst, found_mode);
      }
      found_mode = mode;
      if (mode == MODE_DEL)
        src = new Line(src_s, src_e);
      else if (mode == MODE_ADD)
        dst = new Line(dst_s, dst_e);
      else {
        src = new Line(src_s, src_e);
        dst = new Line(dst_s, dst_e);
      }
    }
  }
  if (found_mode)
    return new Diff(src, dst, found_mode);
  return NULL;
}

bool ContextAnalyzer::is_ignore(char* line)
{
  if (isdigit(line[0]))
    return false;
  if (line[0] == '<' || line[0] == '>')
    return false;
  return true;
}

char* ContextAnalyzer::parse_line_no(char* line, int* src_s, int* src_e,
                                     int* dst_s, int* dst_e, int* mode)
{
  char* src = line;
  char* dst;
  if (!(dst = strchr(src, 'a')))
    if (!(dst = strchr(src, 'd')))
      if (!(dst = strchr(src, 'c')))
        return NULL;

  if (*dst == 'a') *mode = MODE_ADD;
  if (*dst == 'd') *mode = MODE_DEL;
  if (*dst == 'c') *mode = MODE_MOD;
  *dst = 0;
  dst++;

  *src_e = *src_s = atoi(src);
  *dst_e = *dst_s = atoi(dst);

  if (src = strchr(src, ','))
    *src_e = atoi(++src);

  if (dst = strchr(dst, ','))
    *dst_e = atoi(++dst);
  return line;
}

class Writer
{
public:
  Writer(int colum, int encoding,  FILE* fp = stdout)
    : colum_(colum), encoding_(encoding), fp_(fp), isSelfOpened_(false) {
    if (init(colum_))
      THROW_EXCEPTION("memory short");
  }
  Writer(const char* filename, int colum, int encoding)
    : colum_(colum), encoding_(encoding), isSelfOpened_(true) {
    if (!(fp_ = fopen(filename, "w")))
      THROW_EXCEPTION("fopen(%s) %s", filename, strerror(errno));

    if (init(colum_)) {
      fclose(fp_);
      THROW_EXCEPTION("memory short");
    }
  }
  ~Writer() {
    if (fp_ && isSelfOpened_) fclose(fp_);
    if (left_) free(left_);
    if (right_) free(right_);
  }
  void header(const char* filename);
  void format(int lno, const char* l, int rno, const char* r, char mode);
  void LF();
private:
  int init(int colum);
  char* folding(const char* in, char* out);
  void encoding_check(unsigned char* in);
  void getcolumsz(unsigned char* in, int* sz, int* colum);
  void separator();
  FILE* fp_;
  int colum_;
  int encoding_;
  char* left_;
  char* right_;
  bool isSelfOpened_;
};

int Writer::init(int colum)
{
  int size = (colum / 2 * 3) + 1;
  if (left_ = (char*)malloc(size)) {
    memset(left_, 0, size);
    if (right_ = (char*)malloc(size)) {
      memset(right_, 0, size);
      return 0;
    }
  }
  if (left_) free(left_);
  return -1;
}

void Writer::header(const char* filename)
{
  char l_line[FILENAMESIZE+5];
  char r_line[FILENAMESIZE+5];
  memset(l_line, 0, sizeof(l_line));
  memset(r_line, 0, sizeof(r_line));
  sprintf(l_line, "org: %s", filename);
  sprintf(r_line, "new: %s", filename);
  format(0, l_line, 0, r_line, MODE_EQL);
  separator();
}

void Writer::separator()
{
  memset(left_, 0, sizeof(left_));
  memset(right_, 0, sizeof(right_));
  memset(left_, '-', colum_);
  memset(right_, '-', colum_);
  fprintf(fp_, "------%s-+-+-------%s\n", left_, right_);
}

void Writer::format(int lno, const char* l, int rno, const char* r, char mode)
{
  char lno_str[6];
  char rno_str[6];

  while (l || r) {
    if (l)
      l = folding(l, left_);
    else {
      folding("", left_);
      lno = 0;
    }
    if (r)
      r = folding(r, right_);
    else {
      folding("", right_);
      rno = 0;
    }

    strcpy(lno_str, "     ");
    strcpy(rno_str, "     ");
    if (lno > 0)
      sprintf(lno_str, "%5d", lno);
    if (rno > 0)
      sprintf(rno_str, "%5d", rno);
    lno = rno = 0;

    fprintf(fp_, "%s %s |%c| %s %s\n", lno_str, left_, mode, rno_str, right_);
  }
  return;
}

void Writer::LF()
{
  fprintf(fp_, "\n");
}

char* Writer::folding(const char* _in, char* out)
{
  int sz;
  int col;
  int colsum = 0;
  unsigned char* in = (unsigned char*)_in;

  while (*in) {
    getcolumsz(in, &sz, &col);
    if (colsum + col > colum_) break;
    memcpy(out,in, sz);
    in += sz;
    out += sz;
    colsum += col;
  }
  if (colsum < colum_)
    for (; colsum != colum_; colsum++)
      *out++ = ' ';
  *out = 0;
  if (*in)
    return (char*)in;
  return NULL;
}

void Writer::encoding_check(unsigned char* in)
{
  static unsigned char enc_chk = ENCODING_UTF8 | ENCODING_EUC | ENCODING_SJIS;

  if (encoding_ == ENCODING_UNKNOWN) {
    if (!is_ascii(*in)) {
      if (!is_utf8_2byte(in) && !is_utf8_3byte(in)) {
        enc_chk &= ~ENCODING_UTF8;
      }
      if (!is_euc_hankana(in) && !is_euc_zenkaku(in)) {
        enc_chk &= ~ENCODING_EUC;
      }
      if (!is_sjis_hankana(*in) && !is_sjis_zenkaku(in)) {
        enc_chk &= ~ENCODING_SJIS;
      }
      if (enc_chk == ENCODING_UTF8) {
        encoding_ = ENCODING_UTF8;
      }
      if (enc_chk == ENCODING_EUC) {
        encoding_ = ENCODING_EUC;
      }
      if (enc_chk == ENCODING_SJIS) {
        encoding_ = ENCODING_SJIS;
      }
    }
  }
}

void Writer::getcolumsz(unsigned char* in, int* sz, int* col)
{
  *sz = 1;
  *col = 1;

  //encoding_check(in);

  if (encoding_ == ENCODING_UNKNOWN) {
    if (is_ascii(*in)) {
      *sz = 1;
      *col = 1;
    } else if (is_utf8_3byte(in)) {
      *sz = 3;
      *col = 2;
    } else if (is_utf8_2byte(in)  ||
               is_euc_zenkaku(in) ||
               is_sjis_zenkaku(in)) {
      *sz = 2;
      *col = 2;
    } else if (is_euc_hankana(in)) {
      *sz = 2;
      *col = 1;
    } else if (is_sjis_hankana(*in)) {
      *sz = 1;
      *col = 1;
    }
  } else if (encoding_ == ENCODING_UTF8) {
    if (is_utf8_2byte(in)) {
      *sz = 2;
      *col = 2;
    } else if (is_utf8_3byte(in)) {
      *sz = 3;
      *col = 2;
    }
  } else if (encoding_ == ENCODING_SJIS) {
    if (is_sjis_hankana(*in)) {
      *sz = 1;
      *col = 1;
    } else if (is_sjis_zenkaku(in)) {
      *sz = 2;
      *col = 2;
    }
  } else if (encoding_ == ENCODING_EUC) {
    if (is_euc_hankana(in)) {
      *sz = 2;
      *col = 1;
    } else if (is_euc_zenkaku(in)) {
      *sz = 2;
      *col = 2;
    }
  }
}

class Printer
{
public:
  Printer(Analyzer* analyzer, Writer* writer)
    : analyzer_(analyzer), writer_(writer), reader_(0), sno_(0), dno_(0) {}
  ~Printer() {
    delete analyzer_;
    delete writer_;
  }
  void print();
private:
  void print_equal_line(Diff* diff);
  void print_diff_line(Diff* diff);
  const char* reader_skip(int count);
  Reader* reader();
  void print_final();
  Analyzer* analyzer_;
  Writer* writer_;
  Reader* reader_;
  const char* filename_;
  int sno_;
  int dno_;
};

void Printer::print()
{
  while (filename_ = analyzer_->getsrc()) {
    writer_->header(filename_);
    while (Diff* diff = analyzer_->getdiff()) {
      // diff->debug();
      print_equal_line(diff);
      print_diff_line(diff);
      delete diff;
    }
    print_final();
    writer_->LF();
    writer_->LF();
  }
}

void Printer::print_equal_line(Diff* diff)
{
  Line* src = diff->src();
  Line* dst = diff->dst();
  int s_l = 0, d_l = 0, e_l = 0;

  if (src) s_l = (src->start() - 1) - sno_;
  if (dst) d_l = (dst->start() - 1) - dno_;

  if (s_l > 0) {
    e_l = s_l;
    if (d_l > 0)
      e_l = (e_l < d_l) ? e_l : d_l;
  } else {
    e_l = d_l;
  }

  while (e_l--) {
    char* line = reader()->readline();
    sno_++;
    dno_++;
    writer_->format(sno_, line, dno_, line, MODE_EQL);
  }
}

void Printer::print_diff_line(Diff* diff)
{
  Line* src = diff->src();
  Line* dst = diff->dst();

  while (1) {
    std::string *s_str = NULL, *d_str = NULL;
    const char *s_l = NULL, *d_l = NULL;

    if (src) {
      if (s_str = src->getstr()) {
        s_l = s_str->c_str();
        sno_++;
      }
    }
    if (dst) {
      if (d_str = dst->getstr()) {
        d_l = d_str->c_str();
        dno_++;
      }
    }
    if (!s_l && !d_l) break;
    writer_->format(sno_, s_l, dno_, d_l, diff->mode());
    if (s_str) delete s_str;
    if (d_str) delete d_str;
  }

  if (dst)
    reader_skip(dst->end() - (dst->start()-1));
}

const char* Printer::reader_skip(int count)
{
  while(count--) {
    reader()->readline();
  }
}

Reader* Printer::reader()
{
  if (!reader_) reader_ = new Reader(filename_);
  return reader_;
}

void Printer::print_final()
{
  if (reader_) {
    while (char* line = reader_->readline()) {
      sno_++;
      dno_++;
      writer_->format(sno_, line, dno_, line, MODE_EQL);
    }
    delete reader_;
  }
  reader_ = 0;
  sno_ = 0;
  dno_ = 0;
}

void print_version()
{
  fprintf(stderr, "version: %s (compile at %s %s)\n", VERSION, __DATE__, __TIME__);
}

void print_help(const char* prog)
{
  if (strrchr(prog, '/')) {
    prog = strrchr(prog, '/');
    prog++;
  }
  const char* msg =
    "%s [-h|-v|-c colum|-f difftext|-d old_src_dir"
    "|--euc|--sjis|--utf8|--usage|]\n";
  fprintf(stderr, msg, prog);
}

void print_usage(const char* prog)
{
  if (strrchr(prog, '/')) {
    prog = strrchr(prog, '/');
    prog++;
  }
  const char* msg =
    "ex.1) cvs diff | %s > outfile\n"
    "ex.2) svn diff | %s > outfile\n"
    "ex.3) svn diff | %s --utf8 > outfile\n"
    "ex.4) cvs diff -c 120 | %s > outfile\n"
    "ex.5) %s < difftext > outfile\n"
    "ex.6) %s -f difftext > outfile\n"
    "ex.7) %s -d ../old_src_dir > outfile\n";
  fprintf(stderr, msg,
          prog, prog, prog, prog, prog, prog, prog);
}

struct option
{
  const char* difftext;
  const char* old_src_dir;
  int colum;
  int encoding;
};

int parse_arg(int argc, char** argv, struct option* opt)
{
  for (int i = 1; i < argc; i++) {
    const char* arg = argv[i];
    if (!strlen(arg)) continue;
    if (*arg == '-') {
      if (!strcmp(arg, "-f")) {
        if (++i >= argc) return -1;
        opt->difftext = argv[i];
        continue;
      }
      if (!strcmp(arg, "-d")) {
        if (++i >= argc) return -1;
        opt->old_src_dir = argv[i];
        continue;
      }
      if (!strcmp(arg, "-c")) {
        if (++i >= argc) return -1;
        opt->colum = atoi(argv[i]);
        continue;
      }
      if (!strcmp(arg, "-h")) {
        print_help(argv[0]);
        return -1;
      }
      if (!strcmp(arg, "-v")) {
        print_version();
        return -1;
      }
      if (!strcmp(arg, "--euc")) {
        if (opt->encoding == ENCODING_UNKNOWN)
          opt->encoding = ENCODING_EUC;
        continue;
      }
      if (!strcmp(arg, "--sjis")) {
        if (opt->encoding == ENCODING_UNKNOWN)
          opt->encoding = ENCODING_SJIS;
        continue;
      }
      if (!strcmp(arg, "--utf8")) {
        if (opt->encoding == ENCODING_UNKNOWN)
          opt->encoding = ENCODING_UTF8;
        continue;
      }
      if (!strcmp(arg, "--usage")) {
        print_usage(argv[0]);
        return -1;
      }
      fprintf(stderr, "illigal option: %s\n", arg);
      return -1;
    }
  }
  return 0;
}

int main(int argc, char** argv)
{
  struct option opt;
  memset(&opt, 0, sizeof(opt));
  opt.colum = DEFAULT_COLUM;
  if (parse_arg(argc, argv, &opt) < 0)
    return -1;

  FILE* fp = NULL;
  try {
    Reader* reader;
    if (opt.difftext)
      reader = new Reader(opt.difftext);
    else if (opt.old_src_dir) {
      std::string cmd("diff ");
      cmd.append(opt.old_src_dir);
      cmd.append(" .");
      if (!(fp = popen(cmd.c_str(), "r")))
        THROW_EXCEPTION("popen(%s) %s\n", cmd.c_str(), strerror(errno));
      reader = new Reader(fp);
    } else reader = new Reader(); // stdin
    Analyzer* analyzer = Analyzer::create(reader);
    Printer* printer = new Printer(analyzer, new Writer(opt.colum, opt.encoding));
    printer->print();
    delete printer;
  } catch (AppException& e) {
    fprintf(stderr, "%s\n", e.what());
  }
  if (fp) pclose(fp);

  return 0;
}
