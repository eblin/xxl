#include <limits.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/tcp.h>

#ifdef THREAD
#include <pthread.h>
#endif

/* macros and helpers, function prototypes */
#include "def.h"
#include "proto.h"

/* global variables :( */
static I8 PF_ON=0;
static I8 PF_LVL=0;
#define GOBBLERSZ 50
static VP MEM_RECENT[GOBBLERSZ] = {0};
static I8 MEM_W=0; //watching memory?
#define N_MEM_PTRS 1024
static VP MEM_PTRS[N_MEM_PTRS]={0};
static I32 MEM_ALLOC_SZ=0,MEM_FREED_SZ=0;
static I32 MEM_ALLOCS=0, MEM_REALLOCS=0, MEM_FREES=0, MEM_GOBBLES=0;
static VP TAGS=NULL;

#ifdef THREAD
static pthread_mutex_t mutmem=PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t muttag=PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t mutthr=PTHREAD_MUTEX_INITIALIZER;
#define MAXTHR 128
static int NTHR=0;
static pthread_t THR[MAXTHR]={0};
#define WITHLOCK(name,code) ({ pthread_mutex_lock(&mut##name); code; pthread_mutex_unlock(&mut##name); })
#else
#define WITHLOCK(name,code) ({ code; })
#endif

#include "accessors.h"
#include "vary.h"

/*
char* repr_1(VP x,char* s,size_t sz) {
	APF(sz,"[ unary func = %p ],",x);
	return s;
}
char* repr_2(VP x,char* s,size_t sz) {
	APF(sz,"[ binary func = %p ],",x);
	return s;
}
char* repr_p(VP x,char* s,size_t sz) {
	APF(sz,"[ projection = %p ],",x);
	return s;
}
*/
char* repr0(VP x,char* s,size_t sz) {
	type_info_t t;
	if(x==NULL) { APF(sz,"/*null*/",0); return s; }
	t=typeinfo(x->t);
	if(0&&DEBUG) {
		APF(sz," /*%p %s tag=%d#%s itemsz=%d n=%d rc=%d*/ ",x,t.name,
			x->tag,(x->tag!=0 ? sfromx(tagname(x->tag)) : ""),
			x->itemsz,x->n,x->rc);
	}
	if(x->tag!=0) 
		APF(sz, "`%s(", sfromx(tagname(x->tag)));
	if(t.repr) (*(t.repr)(x,s,sz));
	if(x->tag!=0)
		APF(sz, ")", 0);
	return s;
}
char* reprA(VP x) {
	#define BS 1024
	char* s = calloc(1,BS);
	s = repr0(x,s,BS);
	APF(BS,"\n",0);
	return s;
}
VP repr(VP x) {
	char* s = reprA(x);
	return xfroms(s);
}
char* repr_l(VP x,char* s,size_t sz) {
	int i=0, n=x->n;VP a;
	APF(sz,"[",0);
	for(i=0;i<n;i++){
		a = ELl(x,i);
		repr0(a,s,sz);
		if(i!=n-1)
			APF(sz,", ",0);
			// APF(sz,", ",0);
		// repr0(*(EL(x,VP*,i)),s,sz);
	}
	APF(sz,"]",0);
	return s;
}
char* repr_c(VP x,char* s,size_t sz) {
	int i=0,n=x->n,ch;
	APF(sz,"\"",0);
	for(;i<n;i++){
		ch = AS_c(x,i);
		if(ch=='"') APF(sz,"\\", 0);
		if(ch=='\n') APF(sz,"\\n", 0);
		else APF(sz,"%c",ch);
		// repr0(*(EL(x,VP*,i)),s,sz);
	}
	APF(sz,"\"",0);
	return s;
}
char* repr_t(VP x,char* s,size_t sz) {
	int i=0,n=x->n,tag;
	if(n>1) APF(sz,"[",0);
	for(;i<n;i++){
		tag = AS_t(x,i);
		APF(sz,"`%s",sfromx(tagname(tag)));
		if(i!=n-1)
			APF(sz,",",0);
		// repr0(*(EL(x,VP*,i)),s,sz);
	}
	if(n>1) APF(sz,"]",0);
	return s;
}
char* repr_x(VP x,char* s,size_t sz) {
	int i;VP a;
	APF(sz,"[ ",0);
	for(i=0;i<x->n;i++){
		a = ELl(x,i);
		APF(sz,"%d:",i);
		repr0(a,s,sz);
		APF(sz,", ",0);
		// repr0(*(EL(x,VP*,i)),s,sz);
	}
	APF(sz," ]",0);
	return s;
}
char* repr_d(VP x,char* s,size_t sz) {
	int i, n;
	VP k=KEYS(x),v=VALS(x);
	if (!k || !v) { APF(sz,"[null]",0); return s; }
	APF(sz,"[",0);
	n=k->n;
	for(i=0;i<n;i++) {
		repr0(apply(k,xi(i)), s, sz);
		APF(sz,":",0);
		repr0(apply(v,xi(i)), s, sz);
		if(i!=n-1)
			APF(sz,",",0);
	}
	APF(sz,"]",0);
	return s;
}
#include "repr.h"
#include "types.h"

type_info_t typeinfo(type_t n) { ITER(TYPES,sizeof(TYPES),{ IF_RET(_x.t==n,_x); }); }
type_info_t typechar(char c) { ITER(TYPES,sizeof(TYPES),{ IF_RET(_x.c==c,_x); }); }
VP xalloc(type_t t,I32 initn) {
	VP a; int g,i,itemsz,sz; 
	initn = initn < 4 ? 4 : initn;
	itemsz = typeinfo(t).sz; sz=itemsz*initn;
	//PF("%d\n",sz);
	a=NULL;g=0;
	if (GOBBLERSZ > 0) {
		WITHLOCK(mem, {
			FOR(0,GOBBLERSZ,({
				if(MEM_RECENT[_i]!=0 && 
					 ((VP)MEM_RECENT[_i])->sz > sz &&  // TODO xalloc gobbler should bracket sizes
					 ((VP)MEM_RECENT[_i])->sz < (sz * 20)) {
					a=MEM_RECENT[_i];
					MEM_RECENT[_i]=0;
					MEM_GOBBLES++;
					g=_i;
					memset(BUF(a),0,a->sz);
					break;
				}
			}));
		});
	} 
	if(a==NULL)
		a = calloc(sizeof(struct V)+sz,1);
	if (MEM_W) {
		WITHLOCK(mem, {
			MEMPF("%salloc %d %p %d (%d * %d) (total=%d, freed=%d, bal=%d)\n",(g==1?"GOBBLED! ":""),t,a,sizeof(struct V)+sz,initn,itemsz,MEM_ALLOC_SZ,MEM_FREED_SZ,MEM_ALLOC_SZ-MEM_FREED_SZ);
			MEM_ALLOC_SZ += sizeof(struct V)+sz;
			MEM_ALLOCS++;
			for(i=0;i<N_MEM_PTRS;i++) {
				if (MEM_PTRS[i]==0)
					MEM_PTRS[i]=a;
			}
		});
	}
	a->t=t;a->tag=0;a->n=0;a->rc=1;a->cap=initn;a->sz=sz;a->itemsz=itemsz;
	return a;
}
VP xprofile_start() {
	MEM_W=1;
}
VP xprofile_end() {
	int i;
	VP ctx;
	VP res; 
	MEM_W=0;
	printf("allocs: %d (%d), gobbles: %d, reallocs: %d, frees: %d\n", MEM_ALLOC_SZ, MEM_ALLOCS, MEM_GOBBLES, MEM_REALLOCS, MEM_FREES);
	for(i=0;i<N_MEM_PTRS;i++)
		if(MEM_PTRS[i]!=0) {
			xfree(MEM_PTRS[i]);
			MEM_PTRS[i]=0;
		}
	
	// 0..999:        4953
	// 1000..1999:    24
	// 2000..2999:    293
	// 3000..3999:    100
	//
	// count each group 1000 xbar capacity each MEM_PTRS
	/*
	xxl(
		(ctx,"sizes",xxl(MEMPTRS,x2(&map),x1(&capacity),x2(&map),x2(&xbar),xi(1000),0))
	*/
}
VP xrealloc(VP x,I32 newn) {
	// PF("xrealloc %p %d\n",x,newn);
	if(newn>x->cap) {
		buf_t newp; I32 newsz;
		newn = (newn < 10*1024) ? newn * 4 : newn * 1.25;
		newsz = newn * x->itemsz;
		if(x->alloc) {
			// PF("realloc %p %d %d %d\n", x->dyn, x->sz, newn, newsz);
			newp = realloc(x->dyn, newsz);
		} else {
			// PF("calloc sz=%d, n=%d, newn=%d, newsz=%d\n", x->sz, x->n, newn, newsz);
			newp = calloc(newsz,1);
			memmove(newp,BUF(x),x->sz);
		}
		if(MEM_W) {
			// MEMPF("realloc %d %p -> %d\n", x->t, x, newsz);
			MEM_ALLOC_SZ += newsz;
			MEM_REALLOCS++;
		}
		// PF("realloc new ptr = %p\n", newp);
		if(newp==NULL) perror("realloc");
		x->dyn=newp;
		x->cap=newn;
		x->sz=newsz;
		x->alloc=1;
		// PF("post realloc\n"); DUMP(x);
	}
	return x;
}
VP xfree(VP x) {
	int i;
	if(x==NULL)return x;
	//PF("xfree(%p)\n",x);
	//printf("
	x->rc--; 
	if(x->rc==0){
		if(LISTDICT(x))
			ITERV(x,xfree(ELl(x,_i)));
		if(MEM_W) {
			MEM_FREED_SZ+=sizeof(struct V) + x->sz;
			MEM_FREES+=1;
			MEMPF("free %d %p %d (%d * %d) (total=%d, freed=%d, bal=%d)\n",x->t,x,x->sz,x->itemsz,x->cap,MEM_ALLOC_SZ,MEM_FREED_SZ,MEM_ALLOC_SZ-MEM_FREED_SZ);
		}
		if (GOBBLERSZ > 0) {
			for(i=0;i<GOBBLERSZ;i++)
				if(MEM_RECENT[i]==0) {
					MEM_RECENT[i]=x;
					return x;
				}
		}
		free(x);
		//PF("free%p\n",x);free(x);
	} return x; }
VP xref(VP x) { if(MEM_W){MEMPF("ref %p\n",x);} x->rc++; return x; }
VP xfroms(const char* str) {  // character value from string - strlen helper
	size_t len = strlen(str); type_info_t t = typechar('c');
	VP a = xalloc(t.t,len); memcpy(BUF(a),str,len); a->n=len; return a; }
const char* sfromx(VP x) { 
	if(x==NULL)return "null";
	return (char*)BUF(x); }

// RUNTIME!!
VP appendbuf(VP x,buf_t buf,size_t nelem) {
	int newn;buf_t dest;
	newn = x->n+nelem;
	x=xrealloc(x,newn);
	// PF("after realloc"); DUMP(x);
	dest = ELi(x,x->n);
	memmove(dest,buf,x->itemsz * nelem);
	x->n=newn;
	// PF("appendbuf newn %d\n", newn); DUMPRAW(dest, x->itemsz * newn);
	return x;
}
VP append(VP x,VP y) { // append all items of y to x. if x is a general list, append pointer to y, and increase refcount.
	// PF("append %p %p\n",x,y); DUMP(x); DUMP(y);
	ASSERT(LISTDICT(x) || (x->t==y->t), "append(): x must be list, or types must match");
	if(IS_d(x)) {
		ASSERT(y->n % 2 == 0, "append to a dict with [`key;`value]");
		VP k=KEYS(x),v=VALS(x),y1,y2; int i;
		y1=ELl(y,0);
		y2=ELl(y,1);
		// tough decisions
		if(k==NULL) { // create dict
			if(SCALAR(y1)) {
				k=xalloc(y1->t, 4);
			} else {
				k=xl0();
			}
			v=xl0();
			xref(k);xref(v);
			EL(x,VP,0)=k;
			EL(x,VP,1)=v;
			// PF("dict kv %p %p\n", k, v); DUMP(k); DUMP(v);
			i=-1;
		} else 
			i=_find1(k,y1);
		if(i==-1) {
			xref(y1);xref(y2);
			EL(x,VP,0)=append(k,y1);
			EL(x,VP,1)=append(v,y2);
		} else {
			xref(y2);
			ELl(v,i)=y2;
		}
		return x;
	}
	if(LIST(x)) { 
		// PF("append %p to list %p\n", y, x); DUMP(x);
		x=xrealloc(x,x->n+1);
		xref(y);
		EL(x,VP,x->n)=y;
		x->n++;
		// PF("afterward:\n"); DUMP(x);
	} else {
		buf_t dest;
		dest = BUF(x) + (x->n*x->itemsz);
		x=xrealloc(x,x->n + y->n);
		memmove(ELsz(x,x->itemsz,x->n),BUF(y),y->sz);
		x->n+=y->n;
	}
	return x;
}
VP appendfree(VP x,VP y) {
	append(x,y); xfree(y); return x;
}
VP upsert(VP x,VP y) {
	if(_find1(x,y)==-1) append(x,y); return x;
}
int _upsertidx(VP x,VP y) {
	int idx = _find1(x,y);
	if(idx>-1) return idx;
	append(x,y); return x->n-1;
}
inline VP assign(VP x,VP k,VP val) {
	if(DICT(x)) {
		return append(x,xln(2,k,val));
	}
	// TODO assign should support numeric indices
	return EXC(Tt(type),"assign: bad types",x,0);
}
inline VP assigns(VP x,const char* key,VP val) {
	return assign(x,xfroms(key),val);
}
VP flatten(VP x) {
	int i,t=-1;VP res;
	if(!LIST(x))return x;
	if(x->n) {
		t=ELl(x,0)->t; res=xalloc(t,x->n);
		for(i=0;i<x->n;i++) {
			if(ELl(x,i)->t!=t) {
				xfree(res); return x;
			} else
				append(res,ELl(x,i));
		}
	}
	return res;
}
VP drop_(VP x,int i) {
	VP res;
	int st, end;
	if(i<0) { st = 0; end=x->n-i; }
	else { st = i; end=x->n; }
	PF("drop_(,%d) %d %d",i, st, end);
	DUMP(x);
	res = xalloc(x->t,end-st);
	DUMP(info(res));
	if(end-st > 0) {
		appendbuf(res, ELi(x,st), end-st);
	}
	PF("drop_ result\n"); DUMP(res);
	return res;
}
VP take_(VP x,int i) {
	VP res;
	int st, end;
	if(i<0) { st = x->n+i; end=x->n; }
	else { st = 0; end=i; }
	PF("take_(,%d) %d %d",i, st, end);
	DUMP(x);
	res = xalloc(x->t,end-st);
	DUMP(info(res));
	if(end-st > 0) {
		appendbuf(res, ELi(x,st), end-st);
	}
	PF("take_ result\n"); DUMP(res);
	return res;
}
VP take(VP x,VP y) {
	VP res;
	int typerr=-1;
	size_t st,end; //TODO slice() support more than 32bit indices
	// PF("take args\n"); DUMP(x); DUMP(y);
	IF_RET(!NUM(y), EXC(Tt(type),"slice arg must be numeric",x,y));	
	VARY_EL(y, 0, {if (_x<0) { st=x->n+_x; end=x->n; } else { st=0; end=_x; }}, typerr);
	IF_RET(typerr>-1, EXC(Tt(type),"cant use y as slice index into x",x,y));  
	IF_RET(end>x->n, xalloc(x->t, 0));
	res=xalloc(x->t,end-st);
	if(end-st > 0) 
		res=appendbuf(res,ELi(x,st),end-st);
	// PF("take result\n"); DUMP(res);
	return res;
}
VP replaceleft(VP x,int n,VP replace) { // replace first i values with just 'replace'
	int i;
	ASSERT(LIST(x),"replaceleft arg must be list");
	for(i=0;i<n;i++) xfree(ELl(x,i));
	if(n>1) {
		memmove(ELi(x,1),ELi(x,n),x->itemsz*(x->n-n));
	}
	EL(x,VP,0)=replace;
	x->n=x->n-i;
	return x;
}
VP over(VP x,VP y) {
	PF("over");DUMP(x);DUMP(y);
	IF_RET(!IS_2(y) && !IS_p(y), EXC(Tt(type),"over y must be func or projection",x,y));
	IF_RET(x->n==0, xalloc(x->t, 0));
	VP last,next;
	last=apply(x,xi(0));
	FOR(1,x->n,({
		next=apply(x, xi(_i));
		last=apply(apply(y,last),next);
	}));
	return last;
}
VP join(VP x,VP y) {
	VP res;
	PF("join");DUMP(x);DUMP(y);
	int n = x->n + y->n;
	if(x->t==y->t) {
		res=xalloc(x->t, n);
		appendbuf(res, BUF(x), x->n);
		appendbuf(res, BUF(y), y->n);
		xfree(x); 
	} else {
		if(LIST(x))
			res=append(x,y);
		else {
			res=xlsz(n);
			res=append(res,x);
			res=append(res,y);
			xfree(x);
		}
	}
	xfree(y);
	PF("join result");DUMP(res);
	return res;
}
VP splice(VP x,VP idx,VP replace) {
	int first = AS_i(idx,0),last=first+idx->n;
	VP acc1,acc2;
	PF("splice %d %d..%d",x->n, first,last);DUMP(x);DUMP(idx);DUMP(replace);
	return over(
		xln(3,take_(x,first),replace,drop_(x,last)),
		x2(&join)
	);
}
VP split(VP x,VP tok) {
	// PF("split");DUMP(x);DUMP(tok);
	VP tmp,tmp2;int locs[1024]; int typerr=-1;

	// special case for empty or null tok.. split vector into list
	if(tok->n==0) {
		tmp = xl0();
		VARY_EACH(x,({
			// PF("in split vary_each %c\n",_x);
			tmp2=xalloc(x->t, 1);
			tmp2=appendbuf(tmp2,(buf_t)&_x,1);
			tmp=append(tmp,tmp2);
		}),typerr);
		IF_RET(typerr>-1, EXC(Tt(type),"can't split that type", x, tok));
		// PF("split returning\n");DUMP(tmp);
		return tmp;
	}
	VARY_EACH(x,({
		// if this next sequence matches tok,
		//tmp = match(_x, tok);
		//DUMP(tmp); // nyi
	}), typerr);
	return tmp;
}
inline int _equalm(VP x,int xi,VP y,int yi) {
	// PF("comparing %p to %p\n", ELi(x,xi), ELi(y,yi));
	// PF("_equalm\n"); DUMP(x); DUMP(y);
	if(ENLISTED(x)) { PF("equalm descend x");
		return _equalm(ELl(x,xi),0,y,yi);
	}
	if(ENLISTED(y)) { PF("equalm descend y");
		return _equalm(x,xi,ELl(y,yi),0);
	}
	if(memcmp(ELi(x,xi),ELi(y,yi),x->itemsz)==0) return 1;
	else return 0;
}	
int _equal(VP x,VP y) {
	// TODO _equal() needs to handle comparison tolerance and type conversion
	// TODO _equal should use the new VARY_*() macros, except for general lists
	// PF("_equal\n"); DUMP(x); DUMP(y);
	// if the list is a container for one item, we probably want to match the inner one
	if(LIST(x) && SCALAR(x)) x=ELl(x,0);
	if(LIST(y) && SCALAR(y)) y=ELl(y,0);
	IF_RET(x->n != y->n, 0);
	if(LIST(x) && LIST(y)) { ITERV(x,{ IF_RET(_equal(ELl(x,_i),ELl(y,_i))==0, 0); }); return 1; }
	ITERV(x,{ IF_RET(memcmp(ELb(x,_i),ELb(y,_i),x->itemsz)!=0,0); });
	// PF("_equal=1!\n");
	return 1;
}
int _findbuf(VP x,buf_t y) {
	if(LISTDICT(x)) { ITERV(x,{ 
		IF_RET(_findbuf(ELl(x,_i),y)!=-1,_i);
	}); } else {
		ITERV(x,{ IF_RET(memcmp(ELi(x,_i),y,x->itemsz)==0,_i); });
	}
	return -1;
}
int _find1(VP x,VP y) {
	ASSERT(LIST(x) || (x->t==y->t && y->n==1), "_find1(): x must be list, or types must match with right scalar");
	// PF("find %p %p\n",x,y); DUMP(x); DUMP(y);
	if(LISTDICT(x)) { ITERV(x,{ 
		VP xx;
		xx=ELl(x,_i);
		if(xx!=NULL) 
			IF_RET(_equal(xx,y)==1,_i);
	}); }
	else {
		ITERV(x,{ IF_RET(memcmp(ELi(x,_i),ELi(y,0),x->itemsz)==0,_i); });
	}
	return -1;
}
VP find1(VP x,VP y) {
	return xi(_find1(x,y));
}
int _contains(VP x,VP y) {
	return _find1(x,y)==-1 ? 0 : 1;
}
VP contains(VP x,VP y) {
	return xi(_contains(x,y));
}
inline VP times(VP x,VP y) {
	VP r = xalloc(x->t, x->n);
	ITER2(x,y,{ append(r,xi(_x*_y)); });
	return r;
}
inline VP each(VP obj,VP fun) {
	VP tmp, acc; int n=obj->n;
	acc=xalloc(obj->t, obj->n);
	FOR(0,n,({ tmp=apply(obj, xi(_i)); append(acc, apply(fun, tmp)); }));
	return acc;
}
VP cast(VP x,VP y) { 
	// TODO cast() should short cut matching kind casts 
	#define BUFSZ 128
	VP res; I8 buf[BUFSZ]={0}; int typetag=-1;type_t typenum=-1; 
	// right arg is tag naming a type, use that.. otherwise use y's type
	if(y->t==T_t) typetag=AS_t(y,0); else typenum=y->t;
	#include"cast.h"
	// DUMPRAW(buf,BUFSZ);
	return res;
}
VP len(VP x) {
	return xin(1,x->n);
}
VP capacity(VP x) {
	return xin(1,x->cap);
}
VP itemsz(VP x) {
	return xi(x->itemsz);
}
VP info(VP x) {
	VP res;
	type_info_t t;
	t=typeinfo(x->t);
	res=xd0();
	res=assign(res,Tt(typenum),xi(x->t));
	res=assign(res,Tt(type),xfroms(t.name));
	res=assign(res,Tt(len),len(x));
	res=assign(res,Tt(capacity),capacity(x));
	res=assign(res,Tt(itemsz),itemsz(x));
	res=assign(res,Tt(alloced),xi(x->alloc));
	res=assign(res,Tt(baseptr),xi((int)x));
	res=assign(res,Tt(memptr),xi((int)BUF(x)));
	return res;
}
VP deal(VP x,VP y) {
	IF_EXC(!NUM(x),Tt(type),"deal: left arg must be numeric", x, y);
	IF_EXC(SCALAR(y)&&!NUM(y),Tt(type),"deal: single right arg must be numeric", x, y);
	IF_EXC(!SCALAR(x) || !SCALAR(y), Tt(nyi), "deal: both args must be scalar", x, y);

	int typerr=-1;
	VP acc;
	VARY_EL(x,0,({ typeof(_x)n=_x; acc=xalloc(x->t,_x); // TODO rethink deal in terms of more types
		VARY_EL(y,0,({
			int i; // TODO deal() shouldnt use rand()
			FOR(0,n,({i=rand()%_x;appendbuf(acc,&i,1);}));
		}),typerr);}),typerr);
	return acc;
}
VP til(VP x) {
	VP acc;int i;int typerr=-1;
	PF("TIL!!!!\n"); DUMP(x);
	VARY_EL(x, 0, 
		{ __typeof__(_x) i; acc=xalloc(x->t,_x); acc->n=_x; for(i=0;i<_x;i++) { EL(acc,__typeof__(i),i)=i; } }, 
		typerr);
	IF_RET(typerr>-1, EXC(Tt(type), "til arg must be numeric", x, 0));
	DUMP(acc);
	return acc;
}
VP and(VP x,VP y) {
	int typerr=-1;
	VP acc;
	PF("and\n"); DUMP(x); DUMP(y); // TODO and() and friends should handle type conversion better
	IF_EXC(x->n != y->n, Tt(len), "and arguments should be same length", x, y);	
	if(x->t == y->t) acc=xalloc(x->t, x->n);
	else acc=xlsz(x->n);
	VARY_EACHBOTH(x,y,({ if (_x < _y) appendbuf(acc, &_x, 1); else appendbuf(acc, &_y, 1); }), typerr);
	IF_EXC(typerr != -1, Tt(type), "and arg type not valid", x, y);
	DUMP(acc);
	PF("and result\n"); DUMP(acc);
	return acc;
}
VP or(VP x,VP y) {
	int typerr=-1;
	VP acc;
	PF("and\n"); DUMP(x); DUMP(y); // TODO and() and friends should handle type conversion better
	IF_EXC(x->n != y->n, Tt(len), "and arguments should be same length", x, y);	
	if(x->t == y->t) acc=xalloc(x->t, x->n);
	else acc=xlsz(x->n);
	VARY_EACHBOTH(x,y,({ if (_x > _y) appendbuf(acc, &_x, 1); else appendbuf(acc, &_y, 1); }), typerr);
	IF_EXC(typerr != -1, Tt(type), "and arg type not valid", x, y);
	DUMP(acc);
	PF("and result\n"); DUMP(acc);
	return acc;
}
VP min(VP x) { 
	return over(x, x2(&and));
}
VP max(VP x) { 
	return over(x, x2(&or));
}
VP plus(VP x,VP y) {
}
VP sum(VP x) {
	PF("sum");DUMP(x);
	I128 val=0;int i;
	if(!IS_i(x)) return EXC(Tt(type),"sum argument should be numeric",x,0);
	for(i=0;i<x->n;i++) val+=AS_i(x,i);
	return xo(val);
}
VP labelitems(VP label,VP items) {
	VP res;
	PF("labelitems\n");DUMP(label);DUMP(items);
	res=flatten(items);res->tag=_tagnums(sfromx(label));
	DUMP(res);
	return res;
}
VP mkproj(int type, void* func, VP left, VP right) {
	Proj p;
	VP pv=xpsz(1);
	p.type=type;
	if(type==1) {
		p.f1=func; p.left=left; p.right=0;
	} else {
		p.f2=func; p.left=left; p.right=right;
	}
	EL(pv,Proj,0)=p;
	pv->n=1;
	return pv;
}
VP apply(VP x,VP y) {
	VP res=NULL;int i,typerr=-1;
	//PF("apply\n");DUMP(x);DUMP(y);
	if(DICT(x)) {
		VP k=KEYS(x),v=VALS(x);I8 found;
		if(k==NULL || v==NULL) return NULL;
		res=xi0();
		if(IS_x(y)) {
			ITERV(y,{
				res=match(ELl(y,_i),k);
				PF("context match\n");DUMP(res);
				if(res->n) {
					VP rep;
					rep=ELl(v,_i);
					if(IS_1(rep) || IS_2(rep))
						rep=apply(rep,apply(y,res));
					y=replaceleft(y,res->n,rep);
				}
			});
		} else {
			ITERV(y,{ 
				int idx;
				PF("searching %d\n",_i);
				if(LIST(y)) idx = _find1(k,ELl(y,_i));
				else idx = _findbuf(k,ELi(y,_i));
				if(idx>-1) {
					found=1;
					PF("found at idx %d\n", idx); append(res,xi(idx));
				}
			});
		}
		if(res->n==0) {
			if(x->next!=0) res=apply((VP)x->next, res);
		}
		if(res->n==0) return xl0(); else return apply(v,res);
	}
	if(IS_p(x)) { 
		// if its dyadic
		   // if we have one arg, add y, and call - return result
			 // if we have no args, set y as left, and return x
		// if its monadic
			// if we have one arg already, call x[left], and then apply result with y
			// i think this is right..
		Proj* p; p=(Proj*)ELi(x,0);
		if(!p->left) p->left=y; else if (!p->right) p->right=y;
		if(p->type==1 && p->left) return (*p->f1)(p->left);
		if(p->type==2 && p->left && p->right) return (*p->f2)(p->left,p->right);
	}
	if(IS_1(x)) {
		unaryFunc* f; f=AS_1(x,0); return (*f)(y);
	}
	if(IS_2(x)) {
		return mkproj(2,AS_2(x,0),y,0);
	}
	if(NUM(y)) {
		// index a value with an integer 
		if(y->n==1 && LIST(x)) {
			// special case for generic lists:
			// if you index with one item, return just that item
			// generally you would receive a list back
			// this may potentially become painful later on 
			i = AS_i(y,0); 
			IF_RET(i>=x->n, EXC(Tt(index),"index out of range",x,y));
			VP tmp = ELl(x,i); xref(tmp); return tmp;
		} else {
			res=xalloc(x->t,y->n);
			VARY_EACH(y,appendbuf(res,ELi(x,_x),1),typerr);
			//PF("apply VARY_EACH after\n"); DUMP(res);
			if(typerr>-1) return EXC(Tt(type),"cant use y as index into x",x,y);
			return res;
		}
	}
}

// TAG STUFF:

inline VP tagwrap(VP tag,VP x) {
	return entag(xln(1, x),tag);
}
inline VP tagv(const char* name, VP x) {
	return entags(xln(1,x),name);
}
inline VP entag(VP x,VP t) {
	if(IS_c(t))
		x->tag=_tagnum(t);
	else if (IS_i(t))
		x->tag=AS_i(t,0);
	return x;
}
inline VP entags(VP x,const char* name) {
	x->tag=_tagnums(name);
	return x;
}
inline VP tagname(const I32 tag) {
	VP res;
	// PF("tagname(%d)\n", tag);
	// DUMP(TAGS);
	if(TAGS==NULL) { TAGS=xl0();TAGS->rc=INT_MAX; }
	if(tag>=TAGS->n) return xfroms("unknown");
	res = ELl(TAGS,tag);
	// PF("tagname res\n");
	// DUMP(res);
	return res;
}
inline const char* tagnames(const I32 tag) {
	return sfromx(tagname(tag));
}
inline int _tagnum(const VP s) {
	int i;
	WITHLOCK(tag, {
		if(TAGS==NULL) { TAGS=xl0();TAGS->rc=INT_MAX;upsert(TAGS,xfroms("")); PF("new tags\n"); DUMP(TAGS); }
		i=_upsertidx(TAGS,s);
	});
	// PF("tagnum %s -> %d\n",name,i);
	// DUMP(TAGS);
	return i;
}
inline int _tagnums(const char* name) {
	int t;VP s;
	s=xfroms(name);
	t=_tagnum(s);
	xfree(s);
	return t;
}

// MATCHING

VP nest(VP x,VP y) {
	/* 
		nest() benchmarking: (based on 20k loops of test_nest())
		_equalm (single-value delimeter), with debugging: ~12.5s
		_equalm, without debugging: 3.2s
	*/

	int i,j; VP this, open, close, escape, entag, st, cur, newcur; // result stack
	if(y->n<2)return EXC(Tt(nest),"nest y must be 2 (start/end) or 3 (start/end/escape) values ",x,y);
	PF("nest\n"); DUMP(x); DUMP(y);
	open=apply(y,xi(0));
	close=apply(y,xi(1));
	escape=(y->n==3)?apply(y,xi(2)):NULL;
	entag=(y->n==4)?apply(y,xi(3)):NULL;
	st=xl0();
	cur=xl0();
	cur->next=(buf_t)st;
	append(st,cur);
	for(i=0;i<x->n;i++){
		PF("nest state for iter %d\n", i);
		DUMP(st);
		DUMP(cur);
		this=apply(x,xi(i));
		if(matchpass(this, open)) {
			PF("got start at %d\n", i);
			if(cur->n) {
				newcur=xl0();
				newcur->next=(buf_t)cur;
				append(cur, newcur);
				cur=newcur;
			}
			for(j=0; j<open->n; j++) { // too confusing to express with FOR
				append(cur, apply(x,xi(i++)));
			}
			i--;
			// TODO need a shortcut like applyi(VP,int) - too slow
		}
		else if(escape != NULL && escape->n > 0 && matchpass(this,escape)) // escape char; skip
			i+=2;
		else if(matchpass(this,close)) {
			if(cur->n==0) 
				return EXC(Tt(nest),"nest matched end without start",x,y)
			else {
				PF("found end, unpacking %d\n", st->n-1);
				for(j=0; j<close->n; j++) { // too confusing to express with FOR
					append(cur, apply(x,xi(i++)));
				}
				if(entag!=NULL) {
					cur->tag=AS_t(entag,0);
				}
				i--;
				if(cur->next) {
					PF("adopting cur\n");
					cur=(VP)cur->next;
					DUMP(cur);
				}
				PF("ending unpacked:\n");
				DUMP(cur);
			}
		}  else {
			// TODO need an apply shortcut for C types like applyi(VP,int) - too slow otherwise
			newcur=apply(x,xi(i));
			PF("nest appending elem %d\n", i);
			DUMP(newcur);
			append(cur,newcur);
		}
	}
	return st;
}

inline void matchanyof_(const VP obj,const VP pat,const int max_match,int* n_matched,int* matchidx) {
	int i,j;VP item,rule,tmp;
	int submatches;
	int submatchidx[1024];
	tmp=xi(0);
	for(j=0;i<obj->n;j++) {
		for(i=0;i<pat->n;i++) {
			EL(tmp,int,0)=i;
			item=apply(obj,tmp);
			rule=apply(obj,tmp);
			xfree(item);
		}
	}
}

inline int match_(const VP obj_,int ostart, const VP pat_, int pstart, 
		const int max_match,const int max_fails, 
		int* n_matched,int* matchidx) {
	// abandon all hope, ye who enter here
	int anyof=_tagnums("anyof"),exact=_tagnums("exact"),greedy=_tagnums("greedy"),start=_tagnums("start"),
			not=_tagnums("not");
	int matchopt;
	int io, ip;
	int done,found;
	VP obj,pat,item=NULL,rule=NULL,iov,ipv;
	int submatches;
	int submatchidx[1024];
	int i,tag;
	int gotmatch;
	int io_save;

	#define MATCHOPT(tag) ({ \
		if(tag != 0 && \
			 (tag==anyof||tag==exact||tag==greedy||tag==start\
			  ||tag==not)) { \
			PF("adopting matchopt %d %s\n", tag, tagnames(tag)); \
			matchopt = tag; \
			tag = 0; \
		} }) 

	pat=pat_; obj=obj_;
	tag=pat->tag;

	PF("### MATCH_ ostart=%d pstart=%d mm=%d nm=%d, obj and pat:\n",ostart,pstart,max_match,*n_matched);
	DUMP(obj);DUMP(pat);
	//DUMPRAW(matchidx,max_match);

	matchopt=0;
	MATCHOPT(tag);
	if(ENLISTED(pat)) {
		PF("picked up enlisted pat, grabbed \n");
		pat=ELl(pat,0);
		MATCHOPT(pat->tag);
		DUMP(pat);
	}
	if(matchopt==exact&&obj->n!=pat->n) {
		gotmatch=0;
		PF("exact match sizes don't match"); goto fin;
	}
	io=ostart; ip=pstart; iov=xi(io); ipv=xi(ip);
	done=0; found=0; gotmatch=0;
	if(tag != 0 && tag != obj->tag) {
		gotmatch=0;
		PF("tag mismatch at top level p=%d, o=%d\n", pat->tag, obj->tag); found=0; goto fin;
	}
	if(pat->n==0) { 
		PF("empty pat means success\n");
		gotmatch++;
		matchidx[*n_matched]=ostart;
		(*n_matched)=(*n_matched)+1;
		goto fin;
	}
	while (!done && gotmatch < max_match) {
		found=0;
		EL(iov,int,0)=io; EL(ipv,int,0)=ip;
		if(item)xfree(item); if(rule)xfree(rule);
		item=apply(obj, iov); rule=apply(pat, ipv); found=0;
		PF("((( Match loop top. type=%s obj=%d pat=%d: \n",tagnames(matchopt),io,ip);
		DUMP(item);DUMP(rule);

		if(LIST(rule)) { 
			PF("Attempting submatch due to rule being a list..\n"); DUMP(item); DUMP(rule);
			PFIN();
			submatches=*n_matched;
			memcpy(submatchidx, matchidx, sizeof(submatchidx)/sizeof(int));
			match_(obj,io,rule,0,sizeof(submatchidx)/sizeof(int),0,&submatches,matchidx);
			PFOUT();
			if(submatches - *n_matched > 0) {
				PF("Got %d submatches for obj=%d pat=%d\n",submatches,io,ip);found=1;
				io=io + (submatches - *n_matched) -1;
				*n_matched=submatches-1;
				DUMP(item);
				DUMP(rule);
			}
			goto done;
		}

		if(IS_t(rule)) {
			if(AS_t(rule,0) != item->tag ||
				 (rule->tag != 0 && item->tag != rule->tag)) {
				PF("tag mismatch rule=%d, item=%d\n", rule->tag, item->tag); 
				found=0; goto done;
			}
			PF("))) Tag rule seemed to match; continuing..\n");
			ip++;
			continue;
		}
		// Tags used as arguments in match expressions match the type of the object
		// TODO rethink tag matching - seems ambiguous
		if(!IS_t(rule) && item->t != rule->t) {
			PF("Type mismatch rule=%d, item=%d\n", rule->t, item->t); found=0; goto done;
		}
		if(!IS_t(rule) && rule->n==0) { 
			PF("Empty rule = found\n"); found=1; goto done;
		} // empty rules are just type/tag checks
		if(_equal(item,rule)) {
			if(matchopt == not || rule->tag == not) {
				PF("Match_ equal, but NOT mode = not found\n"); found=0; goto done;
			} else {
				PF("Match_ equal = found\n"); found=1; goto done;
			}
		} else {
			if(matchopt == not || rule->tag == not) {
				PF("Match NOT equal, but NOT mode = found\n"); found=1; goto done;
			}
		}
		PF("here\n"); 

		done:
		if(found) {
			gotmatch++;
			PF("+++ FOUND! result #%d, io=%d, ip=%d..\n", *n_matched, io, ip);
			// DUMP(info(item));
			DUMP(item);
			// DUMP(info(rule));
			DUMP(rule);
			found=0;
			for(i=0;i<*n_matched;i++) {
				if (matchidx[i]==io) found=1;
			}
			if (!found)matchidx[*n_matched]=io;
			found=1;
			if(PF_LVL) {
				PF("so far: ");
				FOR(0,(*n_matched)+1,printf("%d ",matchidx[_i]));
				printf("\n");
			}
			*n_matched=*n_matched+1;
			if(matchopt==anyof) {
				io++;
				ip++;
			} else if (matchopt==exact) {
				io++;
				ip++;
			} else if (matchopt==greedy) {

				if (IS_t(rule) && rule->n==1 && ip==0) {
					// even if we're doing a greedy match, if the first element
					// is a tag, we only want to match the tags of subsequent
					// elements.. so move beyond it even if greedy
					PF("greedy submatch for empty tag == tag match, continue..\n");
					ip++;
					continue;
				}

				PF(">> greedy submatch on obj %d/%d\n", io, obj->n);
				PFIN();
				// if(rule->tag == 0) rule=tagv("start",rule);
				io++;
				io_save=io;
				while (io < obj->n && (submatches=match_(obj, io, rule, 0, max_match, 1, n_matched, matchidx))) {
					io+=submatches;
					gotmatch+=submatches;
					io_save+=submatches;
					PFOUT();
					PF(">> greedy advancing to %d/%d, nm=%d, sm=%d, ", io, obj->n, *n_matched, submatches);
					PF("so far: ");
					FOR(0,(*n_matched),printf("%d ",matchidx[_i]));
					printf("\n");
					PFIN();
				}
				if(gotmatch==0) {
					io=io_save;
					PF("<< no submatches, decreasing io to %d\n", io);
				}
				PFOUT();
				ip++;
				PF("<< inner greedy done at io=%d, increase ip=%d\n", io, ip);
			} else if (matchopt==start) {
				io++;
				ip++;
			} else {
				io++;
				// special case for scalar patterns: try to match all items against it
				if(pat->n > 1) 
					ip++;
			}
		} else {
			PF("--- not found! obj=%d pat=%d (result pos #%d)\n", io, ip, *n_matched);
			if(matchopt==anyof) {
				ip++;
				if(ip==pat->n) {
					PF("restarting anyof on next obj, resetting ip\n");
					io++;
					ip=0;
				}
			} else if(max_fails>=1) {
				goto fin;
			} else if (matchopt==exact) {
				goto fin;
			} else if (matchopt==greedy) {
				io++;
				if(gotmatch) ip++; // if we've already seen a match, and we don't match this time, try next rule
			} else if (matchopt==start) {
				goto fin;
			} else {
				io++;
			}
		}

		if(ip>=pat->n||io>=obj->n) done=1;
		PF("done=%d\n",done);
		PF("))) Match loop end.. done=%d, type=%s, found=%d, obj=%d/%d, pat=%d/%d\n", 
			done, tagnames(matchopt), found, io, obj->n, ip, pat->n);

		/*
		if (io == obj->n || ip == pat->n)
			done=1;
		*/
	}

	fin:
	if(item)xfree(item); if(rule)xfree(rule);
	PF("done in match_ with %d matches ", *n_matched);
	if(PF_LVL) { FOR(0,(*n_matched),printf("%d ",matchidx[_i])); printf("\n"); }
	return gotmatch;
}
int matchpass(VP obj,VP pat) { 
	// degenerate version of match() when we dont care about pass/fail, not results
	int n_matches=0;
	int matchidx[1024]={0};
	match_(obj,0,pat,0,sizeof(matchidx)/sizeof(int),0,&n_matches,&matchidx);
	return n_matches;
}
VP match(VP obj,VP pat) {
	int n_matches=0;
	int matchidx[1024];
	VP acc;
	match_(obj,0,pat,0,sizeof(matchidx)/sizeof(int),0,&n_matches,&matchidx);
	acc=xisz(n_matches); // TODO match() limited to 4bil items due to int as idx
	if(n_matches)
		appendbuf(acc,(buf_t)&matchidx,n_matches);
	PF("match() obj, pat, and result:\n");
	DUMP(obj);
	DUMP(pat);
	DUMP(acc);
	return acc;
}
VP matchexec(VP obj,const VP pats) {
	int i,j;VP rule,res,res2,sel;
	ASSERT(LIST(pats)&&pats->n%2==0,"pats should be a list of [pat1,fn1,pat2,fn2..]");
	PF("matchexec start\n");
	DUMP(obj);
	DUMP(pats);
	for(i=0;i<pats->n;i+=2) {
		PF("matchexec %d\n", i);
		PFIN();
		rule=apply(pats,xi(i));
		res=match(obj,rule);
		PFOUT();
		PF("matchexec match, rule and res:\n");
		DUMP(rule);
		DUMP(res);
		// rules start with an unimportant first item: empty tag for tagmatch
		if(res->n >= rule->n-1) { 
			res2=apply(ELl(pats,i+1),apply(obj,res));
			PF("matchexec after apply");
			DUMP(res2);
			obj=splice(obj,res,res2);
		}
	}	
	PF("matchexec done");
	DUMP(obj);
	return obj;
}
VP eval0(VP ctx,VP code,int level) {
	int i,j; 
	VP k,v,cc,kk,vv,matchres,sel,rep;
	PF("eval0 level %d\n",level);
	DUMP(ctx);
	DUMP(code);
	ASSERT(LIST(code),"eval0 list for now");
	k=KEYS(ctx);v=VALS(ctx);
	ASSERT(k!=NULL&&v!=NULL,"eval0: empty ctx");

	// TODO eval0() should probably be implemented in terms of generic tree descent
	for(i=0;i<code->n;i++) { 
		cc=ELl(code,i);
		if (LIST(cc)) 
			eval0(ctx,cc,level+1);
		// try to match each key in ctx
		for (j=0;j<k->n;j++) {
			kk = ELl(k,j);
			PF("eval0 inner %p\n", kk);DUMP(kk);
			matchres = match(cc,kk);
			if(matchres->n==kk->n) {
				PF("got match\n");DUMP(kk);
				sel = apply(cc,matchres);
				PF("sel\n");DUMP(sel);
				vv=ELl(v,j);
				if(IS_1(vv)) 
					rep=apply(vv,ELl(sel,1));
				PF("rep\n");DUMP(rep);
				EL(code,VP,i)=rep;
			}
		}
	}
	return code;
}
VP mklexer(const char* chars, const char* label) {
	VP res = xlsz(2);
	return xln(2,
		entags(xln(2,
			Tt(raw),
			entags(split(xfroms(chars),xl0()),"anyof")
		),"greedy"),
		mkproj(2,&labelitems,xfroms(label),0)
	);
}
VP mkstr(VP x) {
	return entags(flatten(x),"string");
}

// CONTEXTS:

VP mkctx(VP stackframes,VP code) {
	VP res;
	res=take(stackframes,xi(stackframes->n));
	append(res,code);
	return code;
}
VP ctx_resolve(VP ctx) {

}
VP eval(VP code) {
	VP ctx = xd0();VP tmp;
	ASSERT(LIST(code),"eval(): code must be list");
	tmp=xl0();
	tmp=append(tmp,xt(_tagnums("til")));
	tmp=append(tmp,xi0());
	ASSERT(tmp->n==2,"eval tmp n");
	ctx=append(ctx,xln(2, tmp, x1(&til) ));
	tmp=xl0();
	tmp=append(tmp,xi0());
	tmp=append(tmp,xt(_tagnums("+")));
	tmp=append(tmp,xi0());
	ctx=append(ctx,xln(2, tmp, x2(&plus) ));
	return eval0(ctx,code,0);
}
VP evalstr(const char* str) {
	VP lex,pats,acc,t1;size_t l=strlen(str);int i;
	PF("evalstr\n");
	acc=xlsz(l);
	for(i=0;i<l;i++)
		append(acc,entags(xc(str[i]),"raw"));
	if(AS_c(ELl(acc,acc->n - 1),0)!='\n')
		append(acc,entags(xc('\n'),"raw"));
	// DUMP(acc);
	
	// consider using nest instead .. more direct
	// acc=nest(acc,xln(4, xfroms("//"), xfroms("\n"), xfroms(""), Tt(comment)));
	// acc=nest(acc,xln(4, xfroms("//"), xfroms("//"), xfroms(""), Tt(comment)));

	pats=xl0();
	lex = xln(2,
		xln(6,
			Tt(raw),
			xfroms("/"),
			xfroms("/"),
			xl(entags(xl(entags(xl(xc('/')),"not")), "greedy")),
			xfroms("/"),
			xfroms("/")
		),
		mkproj(2,&labelitems,xfroms("comment"),0)
	);
	append(pats,ELl(lex,0));
	append(pats,ELl(lex,1));
	xfree(lex);
	lex = xln(2,
		xln(5,
			Tt(raw),
			xfroms("/"),
			xfroms("/"),
			xl(entags(xl(entags(xl(xc('\n')),"not")), "greedy")),
			//xl(entags(xl(xc0()), "greedy")),
			xfroms("\n")
		),
		mkproj(2,&labelitems,xfroms("comment"),0)
	);
	append(pats,ELl(lex,0));
	append(pats,ELl(lex,1));
	xfree(lex);
	/*
	lex = xln(2,
		entags(xln(2,
			Tt(raw),
			xfroms("\""),
			xc0(),
			xfroms("\"")
		),"greedy"),
		mkproj(2,&labelitems,"string",0)
	);
	append(pats,ELl(lex,0));
	append(pats,ELl(lex,1));
	xfree(lex);
	lex=mklexer("0123456789","int");
	append(pats,ELl(lex,0));
	append(pats,ELl(lex,1));
	xfree(lex);
	lex=mklexer("`abcdefghijlmnopqrstuvwxyz.","name");
	append(pats,ELl(lex,0));
	append(pats,ELl(lex,1));
	xfree(lex);
	lex=mklexer(" \n\t\r","ws");
	append(pats,ELl(lex,0));
	append(pats,ELl(lex,1));
	xfree(lex);
	*/
	t1=matchexec(acc,pats);
	PF("evalstr result\n");
	DUMP(t1);
	return t1;
}

// THREADING

void thr_start() {
	// TODO threading on Windows
	#ifndef THREAD
	return;
	#endif
	NTHR=0;
}
void* thr_run0(void *fun(void*)) {
	#ifndef THREAD
	return;
	#endif
	(*fun)(NULL); pthread_exit(NULL);
}
void thr_run(void *fun(void*)) {
	#ifndef THREAD
	return;
	#endif
	pthread_attr_t a; pthread_attr_init(&a); pthread_attr_setdetachstate(&a, PTHREAD_CREATE_JOINABLE);
	// nthr=sysconf(_SC_NPROCESSORS_ONLN);if(nthr<2)nthr=2;
	WITHLOCK(thr,pthread_create(&THR[NTHR++], &a, &thr_run0, fun));
}
void thr_wait() {
	#ifndef THREAD
	return;
	#endif
	void* _; int i; for(i=0;i<NTHR;i++) pthread_join(THR[i],&_);
}

void test_basics() {
	printf("TEST_BASICS\n");
	#include "test-basics.h"
}
void test_proj() {
	VP a,b,c,n;
	printf("TEST_PROJ\n");
	n=xi(1024*1024);
	//a=mkproj(1,&til,n,0);
	a=x1(&til);
	b=apply(a,n);
	PF("b\n");DUMP(b);
	c=apply(mkproj(1,&sum,b,0),0);
	PF("result\n");DUMP(c);
	printf("%lld\n", AS_o(c,0));
	xfree(a);xfree(b);xfree(c);xfree(n);
	//DUMP(c);
}
void test_proj_thr0(void* _) {
	VP a,b,c,n; int i;
	for (i=0;i<1024;i++) {
		printf("TEST_PROJ %d\n", pthread_self());
		n=xi(1024*1024);
		//a=mkproj(1,&til,n,0);
		a=x1(&til);
		b=apply(a,n);
		PF("b\n");DUMP(b);
		c=apply(mkproj(1,&sum,b,0),0);
		PF("result\n");DUMP(c);
		printf("%lld\n", AS_o(c,0));
		xfree(a);xfree(b);xfree(c);xfree(n);
	}
	return NULL;
}
void test_proj_thr() {
	int n = 2, i; void* status;
	/*
	pthread_attr_t a;
	pthread_t thr[n];
	pthread_attr_init(&a);
	pthread_attr_setdetachstate(&a, PTHREAD_CREATE_JOINABLE);
	for(i=0;i<n;i++) {
		pthread_create(&thr[i], &a, test_proj_thr0, NULL);
	}
	for(i=0; i<n; i++) {
		pthread_join(&thr[i], &status);
	}
	*/
	thr_start();
	for(i=0;i<n;i++) thr_run(test_proj_thr0);
	thr_wait();
}
void test_context() {
	VP a,b,c;
	printf("TEST_CONTEXT\n");
	a=xd0();
	assigns(a,"til",x1(&til));
	assign(a,tagv("greedy",xln(3,xfroms("\""),xc0(),xfroms("\""))),x1(&mkstr));
	b=xxn(7,xfroms("\""),xfroms("a"),xfroms("b"),xfroms("c"),xfroms("\""),xfroms("til"),xfroms("1024"));
	c=apply(a,b);
	DUMP(a);
	DUMP(b);
}
void test_match() {
	#include"test-match.h"
}
void test_eval() {
	printf("TEST_EVAL\n");
	PFW({
	ASSERT(
		_equal(
			evalstr("// "),
			xl(entags(xfroms("// \n"),"comment"))
		), "tec0");
	ASSERT(
		_equal(
			evalstr("// //"),
			xln(2,entags(xfroms("// //"),"comment"),xfroms("\n"))
		), "tec0b");
	ASSERT(
		_equal(
			evalstr("//a////z//"),
			xln(3,
				entags(xfroms("//a//"),"comment"),
				entags(xfroms("//z//"),"comment"),
				xfroms("\n"))
		), "tec1b");
	ASSERT(
		_equal(
			evalstr("//x"),
			xl(entags(xfroms("//x\n"),"comment"))
		), "tec1");
	ASSERT(
		_equal(
			evalstr("//x//"),
			xln(2,entags(xfroms("//x//"),"comment"),xfroms("\n"))
		), "tec1b");
	ASSERT(
		_equal(
			evalstr("//xy"),
			xl(entags(xfroms("//xy\n"),"comment"))
		), "tec1c");
	ASSERT(
		_equal(
			evalstr("//x "),
			xl(entags(xfroms("//x \n"),"comment"))
		), "tec2");
	ASSERT(
		_equal(
			evalstr("// x"),
			xl(entags(xfroms("// x\n"),"comment"))
		), "tec2b");
	ASSERT(
		_equal(
			evalstr("// abc "),
			xl(entags(xfroms("// abc \n"),"comment"))
		), "tec3");
	ASSERT(
		_equal(
			evalstr("// a\n//b"),
			xln(2,
				entags(xfroms("// a\n"),"comment"),
				entags(xfroms("//b\n"),"comment"))
		), "tec4");
	ASSERT(
		_equal(
			evalstr("// abc //"),
			xln(2,entags(xfroms("// abc //"),"comment"),xfroms("\n"))
		), "tec5");
	ASSERT(
		_equal(
			evalstr("1"),
			xln(2,entags(xfroms("1"),"int"),xfroms("\n"))
		), "tei0");
	ASSERT(
		_equal(
			evalstr("1//blah"),
			xln(2,
				entags(xfroms("1"),"int"),
				entags(xfroms("//blah\n"),"comment"))
		), "teic0");
	ASSERT(
		_equal(
			evalstr("1//blah\n2"),
			xln(4,
				entags(xfroms("1"),"int"),
				entags(xfroms("//blah\n"),"comment"),
				entags(xfroms("2"),"int"),
				xfroms("\n")
			)
		), "teic0");
	//DUMP(evalstr("// test"));
	//evalstr("// test\nx:\"Hello!\"\ntil 1024");
	});
}
void test_json() {
	VP mask, jsrc, res; char str[256]={0};
	strncpy(str,"[[\"abc\",5,[\"def\"],6,[7,[8,9]]]]",256);
	jsrc=split(xfroms(str),xc0());
	DUMP(jsrc);
	res=nest(jsrc,xln(2,xfroms("["),xfroms("]")));
	DUMP(res);
	DUMP(each(res, x1(&repr)));
}
void test_nest() {
	VP a,b,c;
	printf("TEST_NEST\n");
	#include"test-nest.h"
	xfree(a);xfree(b);xfree(c);
}
void tests() {
	int i;
	VP a,b,c;
	// xprofile_start();
	test_basics();
	test_match();
	test_json();
	test_nest();
	test_eval();
	// xprofile_end();
	exit(1);
	test_proj_thr();
	net();
	if(MEM_W) {
		PF("alloced = %llu, freed = %llu\n", MEM_ALLOC_SZ, MEM_FREED_SZ);
	}
	exit(1);
}
int main(void) {
	VP code;
	tests();
}
/*
	
	TODO decide operator for typeof
	TODO decide operator for tagof
	TODO decide operator for applytag
	TODO can we auto-parallelize some loops?

*/ 
