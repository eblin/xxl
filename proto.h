// prototypes

// from xxl.c
int _find(VP x,VP y);
int _any(VP x);
VP amend(VP x,VP y);
VP any(VP x);
VP apply(VP x,VP y);
VP append(VP x,VP y);
static VP assign(VP x,VP k,VP val);
VP cast(VP x,VP y);
VP capacity(VP x);
VP condense(VP x);
VP deep(VP obj,VP f);
VP dict(VP x,VP y);
static inline VP each(VP obj,VP fun);
static inline VP entag(VP x,VP t);
static inline VP entags(VP x,const char* name);
int _find1(VP x,VP y);
VP first(VP x);
VP get(VP x,VP y);
VP greater(VP x,VP y); 
VP info(VP x);
VP join(VP x,VP y);
VP itemsz(VP x);
VP last(VP x);
VP len(VP x); 
VP lesser(VP x,VP y); 
VP list2vec(VP obj);
VP match(VP obj,VP pat);
VP matcheasy(VP obj,VP pat);
VP matchtag(VP obj,VP pat);
int matchpass(VP obj,VP pat);
VP mkproj(int type, void* func, VP left, VP right);
VP over(VP x,VP y);
VP parse(VP x);
VP repr(VP x);
char* repr0(VP x,char* s,size_t len);
char* reprA(VP x);
VP reverse(VP x);
VP rotate_(VP x,int i);
VP rotate(VP x,VP y);
VP set(VP x,VP y);
const char* sfromx(VP x);
VP split(VP x,VP tok);
static inline VP tagname(I32 tag);
static inline VP tagv(const char* name, VP x);
VP take_(VP x,int i);
VP take(VP x,VP y);
static inline int _tagnum(VP name);
static inline int _tagnums(const char* name);
static type_info_t typeinfo(type_t n); 
static type_info_t typechar(char c);
VP xalloc(type_t t,I32 initn);
VP xfree(VP x);
VP xfroms(const char* str);
VP xor(VP x,VP y);
VP xrealloc(VP x,I32 newn);

// from net.c
void net(void);
size_t netr(int sock,void* b,size_t maxl);
void netw(int sock,void* b,size_t l);
void netloop(int sock);
