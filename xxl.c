/* macros and helpers, function prototypes */
#include "def.h"
#include "proto.h"

/* global variables :( */
I8 PF_ON=0;
I8 PF_LVL=0;
VP TAGS=NULL;

#define GOBBLERSZ 50
static VP MEM_RECENT[GOBBLERSZ] = {0};
static I8 MEM_W=0; //watching memory?
#define N_MEM_PTRS 1024
static VP MEM_PTRS[N_MEM_PTRS]={0};
static I32 MEM_ALLOC_SZ=0,MEM_FREED_SZ=0;
static I32 MEM_ALLOCS=0, MEM_REALLOCS=0, MEM_FREES=0, MEM_GOBBLES=0;

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
*/
char* repr0(VP x,char* s,size_t sz) {
	type_info_t t;
	if(x==NULL) { APF(sz,"/*null*/",0); return s; }
	t=typeinfo(x->t);
	if(0 && DEBUG) {
		APF(sz," /*%p %s tag=%d#%s itemsz=%d n=%d rc=%d*/ ",x,t.name,
			x->tag,(x->tag!=0 ? sfromx(tagname(x->tag)) : ""),
			x->itemsz,x->n,x->rc);
	}
	if(x->tag!=0) 
		APF(sz, "'%s(", sfromx(tagname(x->tag)));
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
			// APF(sz,",\n",0);
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
	if(n>1) APF(sz,"(",0);
	for(;i<n;i++){
		tag = AS_t(x,i);
		APF(sz,"'%s",sfromx(tagname(tag)));
		if(i!=n-1)
			APF(sz,",",0);
		// repr0(*(EL(x,VP*,i)),s,sz);
	}
	if(n>1) APF(sz,")",0);
	return s;
}
char* repr_x(VP x,char* s,size_t sz) {
	int i;VP a;
	APF(sz,"'ctx[",0);
	for(i=0;i<x->n;i++){
		a = ELl(x,i);
		repr0(a,s,sz);
		if(i!=x->n-1)
			APF(sz,", ",0);
		// repr0(*(EL(x,VP*,i)),s,sz);
	}
	APF(sz,"]",0);
	return s;
}
char* repr_d(VP x,char* s,size_t sz) {
	int i, n;
	VP k=KEYS(x),v=VALS(x);
	if (!k || !v) { APF(sz,"[null]",0); return s; }
	APF(sz,"'dict[",0);
	n=k->n;
	for(i=0;i<n;i++) {
		repr0(apply(k,xi(i)), s, sz);
		APF(sz,":",0);
		repr0(apply(v,xi(i)), s, sz);
		if(i!=n-1)
			APF(sz,", ",0);
	}
	APF(sz,"]",0);
	return s;
}
char* repr_p(VP x,char* s,size_t sz) {
	Proj p = EL(x,Proj,0);
	ASSERT(1,"repr_p");
	APF(sz,"'projection(%p,%d,%p,",x,p.type,p.type==1?p.f1:p.f2);
	if(p.left!=NULL) 
		repr0(p.left, s, sz);
	else
		APF(sz,"()",0);
	APF(sz,",",0);
	if(p.right!=NULL) 
		repr0(p.right, s, sz);
	else
		APF(sz,"()",0);
	APF(sz,")",0);
	return s;
}
#include "repr.h"
#include "types.h"

static inline type_info_t typeinfo(type_t n) { ITER(TYPES,sizeof(TYPES),{ IF_RET(_x.t==n,_x); }); }
static inline type_info_t typechar(char c) { ITER(TYPES,sizeof(TYPES),{ IF_RET(_x.c==c,_x); }); }

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
	return xl0();
}
VP xprofile_end() {
	int i;
	VP ctx;
	VP res; 
	MEM_W=0;
	printf("allocs: %d (%d), gobbles: %d, reallocs: %d, frees: %d\n", MEM_ALLOC_SZ, MEM_ALLOCS, MEM_GOBBLES, MEM_REALLOCS, MEM_FREES);
	for(i=0;i<N_MEM_PTRS;i++)
		if(MEM_PTRS[i]!=0) {
			printf("freeing mem ptr\n");
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
	return xl0();
}
VP xrealloc(VP x,I32 newn) {
	// PF("xrealloc %p %d\n",x,newn);
	if(newn>x->cap) {
		buf_t newp; I32 newsz;
		newn = (newn < 10*1024) ? newn * 4 : newn * 1.25; // TODO there must be research about realloc bins no?
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
	//PF("xfree(%p)\n",x);DUMP(x);//DUMP(info(x));
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
		//PF("xfree(%p) really dropping type=%d n=%d alloc=%d\n",x,x->t,x->n,x->alloc);
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
	//PF("appendbuf %d\n", nelem);DUMP(x);
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
	IF_EXC(!CONTAINER(x) && !(x->t==y->t), Tt(Type), "append x must be container or types must match", x, y);
	if(IS_d(x)) {
		ASSERT(y->n % 2 == 0, "append to a dict with ['key;value]");
		VP k=KEYS(x),v=VALS(x),y1,y2; int i;
		y1=ELl(y,0);
		y2=ELl(y,1);
		// tough decisions
		// PF("append dict\n");DUMP(x);DUMP(k);DUMP(v);
		if(k==NULL) { // create dict
			if(0 && SCALAR(y1)) {
				k=ALLOC_LIKE_SZ(y1, 4);
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
	if(CONTAINER(x)) { 
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
VP amend(VP x,VP y) {
	PF("amend\n");DUMP(x);DUMP(y);
	// TODO amend should create a new structure rather than modifying one in-place
	if(!SIMPLE(x) && !CONTAINER(x))return EXC(Tt(type),"amend x must be a simple or container type",x,y);
	if(!CONTAINER(y) || y->n!=2)return EXC(Tt(type),"amend y should be [indices,replacements]",x,y);
	VP idx=ELl(y,0), val=ELl(y,1), acc=ALLOC_LIKE_SZ(x, idx->n), idxi, idxv, tmp=0; int i,fail=-1;
	if(CALLABLE(idx)) idx=condense(matcheasy(x,idx));
	for(i=0;i<idx->n;i++) { 
		idxi=xi(i); idxv=apply(idx,idxi); // TODO really need a fast 0 alloc version of apply(simple,int)!
		if(UNLIKELY(CALLABLE(val))) tmp=apply(val,apply(x,idxv));
		else {
			if (SCALAR(val)) tmp=xref(val); 
			else tmp=apply(val,idxv);
		}
		if(UNLIKELY(tmp->t!=x->t))return EXC(Tt(value),"amend value type does not match x",x,tmp);
		assign(x,idxv,tmp);
		xfree(idxi); xfree(idxv); xfree(tmp);
	}
	PF("amend returning\n");DUMP(x);
	return x;
}
static inline VP assign(VP x,VP k,VP val) {
	// PF("assign\n");DUMP(x);DUMP(k);DUMP(val);
	if(DICT(x)) {
		xref(k); xref(val);
		return append(x,xln(2,k,val));
	}
	// TODO assign should support numeric indices
	if(NUM(k)) {
		// PF("assign");DUMP(x);DUMP(k);DUMP(val);
		if(x->t != val->t) return EXC(Tt(type),"assign value and target types don't match",x,val);
		int typerr=-1;
		VARY_EACHRIGHT(x,k,({
			EL(x,typeof(_x),_y) = EL(val,typeof(_x),_y%val->n); // TODO assign should create new return value
		}),typerr);
		// PF("assign num returning");DUMP(x);
		return x;
	}
	return EXC(Tt(type),"assign: bad types",x,0);
}
static inline VP assigns(VP x,const char* key,VP val) {
	return assign(x,xfroms(key),val);
}
int _flat(VP x) { // returns 1 if vector, or a list composed of vectors (and not other lists)
	// PF("flat\n");DUMP(x);
	if(!LIST(x)) return 1;
	int typerr=-1;VP tmp;
	FOR(0,x->n,({ tmp=ELl(x,_i); if(tmp->n>1 || !SIMPLE(tmp)) return 0; }));
	// PF("_flat=1");
	return 1;
}
VP flatten(VP x) {
	int i,t=-1;VP res=0;
	if(!LIST(x))return x;
	if(x->n) {
		t=ELl(x,0)->t; res=ALLOC_LIKE(ELl(x,0));
		for(i=0;i<x->n;i++) {
			if(ELl(x,i)->t!=t) {
				xfree(res); return x;
			} else
				append(res,ELl(x,i));
		}
	}
	return res;
}
VP dict(VP x,VP y) {
	PF("dict\n");DUMP(x);DUMP(y);
	if(DICT(x)) {
		if(DICT(y)) return EXC(Tt(nyi),"dict join dict not yet implemented",x,y);
		if(KEYS(x)->n > VALS(x)->n) { // dangling dictionary detection
			append(VALS(x),y);
		} else {
			if(LIST(y) && y->n==2) {
				append(KEYS(x),first(y));
				append(VALS(x),last(y));
			} else {
				append(KEYS(x),y);
			}
		}
		PF("dict already dict returning\n");DUMP(x);
		return x;
	} else {
		if(x->n > 1 && x->n != y->n) return EXC(Tt(value),"can't create dict from unlike vectors",x,y);
		VP d=xd0();
		if(LIKELY(SCALAR(x))) {
			if(LIST(x))  // handle ['a:1] which becomes [['a]:1]
				d=assign(d,ELl(x,0),y);
			else
				d=assign(d,x,y);
		} else {
			int i;VP ii;
			for(i=0;i<x->n;i++) {
				ii=xi(i); d=assign(d,apply(x,ii),apply(y,ii));
			}
		}
		PF("dict new returning\n");DUMP(d);
		return d;
	}
}
VP drop_(VP x,int i) {
	VP res;
	int st, end;
	if(i<0) { st = 0; end=x->n+i; }
	else { st = i; end=x->n; }
	PF("drop_(,%d) %d %d %d\n", i, x->n, st, end);
	// DUMP(x);
	res=ALLOC_LIKE_SZ(x,end-st);
	// DUMP(info(res));
	if(end-st > 0) {
		appendbuf(res, ELi(x,st), end-st);
	}
	PF("drop_ result\n"); DUMP(res);
	return res;
}
VP drop(VP x,VP y) {
	VP res=0;
	int typerr=-1;
	PF("drop args\n"); DUMP(x); DUMP(y);
	IF_RET(!NUM(y) ||!SCALAR(y), EXC(Tt(type),"drop x arg must be single numeric",x,y));	
	VARY_EL(y, 0, ({ return drop_(x,_x); }), typerr);
	return res;
}
VP first(VP x) {
	VP i,r;
	if(CONTAINER(x)) return xref(ELl(x,0));
	else { i=xi(0); r=apply(x,i); xfree(i); return r; }
}
VP identity(VP x) {
	return x;
}
VP join(VP x,VP y) {
	VP res=0;
	PF("join\n");DUMP(x);DUMP(y);
	int n = x->n + y->n;
	if(!LIST(x) && x->tag==0 && x->t==y->t) {
		PF("join2\n");
		res=ALLOC_LIKE_SZ(x, n);
		appendbuf(res, BUF(x), x->n);
		appendbuf(res, BUF(y), y->n);
	} else {
		PF("join3\n");
		if(DICT(x))
			return dict(x,y);
		else if(LIST(x)) {
			int has_structure=0;
			if(y->tag != 0 || x->tag != 0 || (x->n > 0 && ELl(x,x->n-1)->tag != 0)) {
				// avoid joining structured lists - feels hackish
				PF("has_structure\n");
				has_structure=1;
			}
			if(LIST(y) && _flat(y) && !has_structure) 
				FOR(0,y->n,res=append(x,ELl(y,_i)));
			else 
				res=append(x,y);
		} else {
			PF("join4\n");
			res=xlsz(n);
			res=append(res,x);
			res=append(res,y);
		}
	}
	//res=list2vec(res);
	PF("join result");DUMP(res);
	return res;
}
VP last(VP x) {
	VP res=ALLOC_LIKE_SZ(x,1);
	if(x->n==0) return res;
	res=appendbuf(res,ELi(x,x->n-1),1);
	return res;
}
static inline VP list(VP x) { // convert x to general list
	if(LIST(x))return x;
	return split(x,xi0());
}
VP list2(VP x,VP y) { // helper for [ - convert y to general list, drop x
	return xl(y);
}
VP nullfun(VP x) {
	return xl0();
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
VP reverse(VP x) {
	if(!SIMPLE(x)||CONTAINER(x)) return EXC(Tt(type),"reverse arg must be simple or container",x,0);
	int i,typerr=-1; VP acc=ALLOC_LIKE(x);
	for(i=x->n-1;i>=0;i--) appendbuf(acc,ELi(x,i),1);
	return acc;
}
VP shift_(VP x,int i) {
	PF("shift_ %d\n",i);DUMP(x);
	int n=x->n;
	if(i<0) 
		return join(take_(x,i%n),drop_(x,i%n));
	else
		return join(drop_(x,i%n),take_(x,i%n));
}
VP shift(VP x,VP y) {
	//       1 2 3 4 5 6 7
	// sh 1  2 3 4 5 6 7 1 = drop 1 , take -1
	// sh 2  3 4 5 6 7 1 2
	// sh -1 7 1 2 3 4 5 6
	// sh -2 6 7 1 2 3 4 5
	PF("shift\n");DUMP(x);DUMP(y);
	if(!SIMPLE(x)) return EXC(Tt(type),"shr x must be a simple type",x,y);
	if(!NUM(y)) return EXC(Tt(type),"shr y must be numeric",x,y);
	int typerr=-1;
	VARY_EL(y,0,({return shift_(x,_x);}),typerr);
	return (VP)0;
}
VP splice(VP x,VP idx,VP replace) {
	int i, first = AS_i(idx,0),last=first+idx->n;
	VP acc;
	PF("splice (%d len) %d..%d",x->n, first,last);DUMP(x);DUMP(idx);DUMP(replace);
	if(first==0 && last==x->n) return replace;
	acc=xl0();
	if(LIST(x)) {
		for(i=0;i<first;i++)
			acc=append(acc,ELl(x,i));
		append(acc,replace);
		for(i=last;i<x->n;i++)
			acc=append(acc,ELl(x,i));
	} else {
		if(first > 0) 
			acc=append(acc, take_(x, first));
		acc=append(acc, replace);
		if (last < x->n)
			acc=append(acc, drop_(x, last));
		PF("splice calling over\n");DUMP(acc);
		return over(acc, x2(&join));
	}
	PF("splice returning\n"); DUMP(acc);
	return acc;

	/*
	if(first > 0) 
		acc=append(acc, take_(x, first));
	acc=append(acc, replace);
	if (last < x->n)
		acc=append(acc, drop_(x, last));
	PF("splice calling over\n");DUMP(acc);
	return over(acc, x2(&join));
	*/
	/*
	return over(
		xln(3,take_(x,first),replace,drop_(x,last)),
		x2(&join)
	);
	*/
}
VP split(VP x,VP tok) {
	PF("split");DUMP(x);DUMP(tok);
	VP tmp=0,tmp2=0; int locs[1024],typerr=-1;

	// special case for empty or null tok.. split vector into list
	if(tok->n==0) {
		tmp=xl0();
		if(LIST(x))return x;
		VARY_EACHLIST(x,({
			// PF("in split vary_each %c\n",_x);
			tmp2=ALLOC_LIKE_SZ(x, 1);
			tmp2=appendbuf(tmp2,(buf_t)&_x,1);
			tmp=append(tmp,tmp2);
		}),typerr);
		IF_RET(typerr>-1, EXC(Tt(type),"can't split that type", x, tok));
		PF("split returning\n");DUMP(tmp);
		return tmp;
	}
	return EXC(Tt(nyi),"split with non-null token not yet implemented",x,tok);
}
VP take_(VP x,int i) {
	VP res;
	int st, end, xn=x->n;
	/*
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
	*/
	if (i<0) { st=ABS((xn+i)%xn); end=ABS(i)+st; } else { st=0; end=i; }
	// PF("take_(,%d) %d %d\n",i, st, end); DUMP(info(x));
	//IF_RET(end>xn, xalloc(x->t, 0));
	res=ALLOC_LIKE_SZ(x,end-st);
	// PF("take end %d st %d\n", end, st);
	// DUMP(info(res)); DUMP(x);
	FOR(st,end,({ res=appendbuf(res,ELi(x,_i % xn),1); }));
	//if(end-st > 0) 
	//res=appendbuf(res,ELi(x,st),end-st);
	// PF("take result\n"); DUMP(res);
	// DUMP(info(res));
	return res;
}
VP take(VP x,VP y) {
	int typerr=-1;
	size_t st,end; //TODO slice() support more than 32bit indices
	PF("take args\n"); DUMP(x); DUMP(y);
	IF_RET(!NUM(y) ||!SCALAR(y), EXC(Tt(type),"take x arg must be single numeric",x,y));	
	VARY_EL(y, 0, ({ return take_(x,_x); }), typerr);
	return (VP)0;
}
static inline int _equalm(VP x,int xi,VP y,int yi) {
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
int _findbuf(VP x,buf_t y) {   // returns index or -1 on not found
	// PF("findbuf\n");DUMP(x);
	if(LISTDICT(x)) { ITERV(x,{ 
		// PF("findbuf trying list\n"); DUMP(ELl(x,_i));
		IF_RET(_findbuf(ELl(x,_i),y)!=-1,_i);
		// IF_RET(_equal(ELl(x,_i),y)==1,_i);
		// PF("findbuf no list match\n");
	}); } else {
		// PF("findbuf trying vector\n");
		ITERV(x,{ IF_RET(memcmp(ELi(x,_i),y,x->itemsz)==0,_i); });
		// PF("findbuf no vector match\n");
	}
	return -1;
}
int _find1(VP x,VP y) {        // returns index or -1 on not found
	// probably the most common, core call in the code. worth trying to optimize.
	// PF("_find1\n",x,y); DUMP(x); DUMP(y);
	ASSERT(LIST(x) || (x->t==y->t && y->n==1), "_find1(): x must be list, or types must match with right scalar");
	if(LISTDICT(x)) { ITERV(x,{ 
		VP xx; xx=ELl(x,_i);
		// PF("_find1 %d\n",_i); DUMP(xx);
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
VP condense(VP x) {
	// equivalent to k's & / where - condenses non-zeroes into their positions (ugh english)
	// always returns an int vector for now; we generally rely on ints too much for this
	int typerr=-1; int j; VP acc=xi0();
	// PF("condense\n");DUMP(x);
	VARY_EACH(x,({ if(_x) { j=_i; FOR(0,_x,appendbuf(acc,(buf_t)&j,1)); } }),typerr);
	PF("condense returning\n");DUMP(acc);
	return acc;
}
VP cast(VP x,VP y) { 
	// TODO cast() should short cut matching kind casts 
	#define BUFSZ 128
	VP res=0; I8 buf[BUFSZ]={0}; int typetag=-1;type_t typenum=-1; 
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
VP deal(VP range,VP amt) {
	PF("deal\n");DUMP(range);DUMP(amt);
	IF_EXC(!NUM(range),Tt(type),"deal: left arg must be numeric", range, amt);
	IF_EXC(!NUM(range),Tt(type),"deal: single right arg must be numeric", range, amt);
	IF_EXC(!SCALAR(range) || !SCALAR(amt), Tt(nyi), "deal: both args must be scalar", range, amt);

	int typerr=-1;
	VP acc=0;
	VARY_EL(amt,0,({ typeof(_x)amt=_x; acc=ALLOC_LIKE_SZ(range,_x); // TODO rethink deal in terms of more types
		VARY_EL(range,0,({
			FOR(0,amt,({ EL(acc,typeof(_x),_i)=rand()%_x; }));
			acc->n=amt;
		}),typerr);}),typerr);
	return acc;
}

// APPLICATION, ITERATION AND ADVERBS

static inline VP applyexpr(VP parent,VP code,VP xarg,VP yarg) {
	char ch; int i, tcom, texc, tlam, traw, tstr, tws, xused=0, yused=0; 
	VP left,item;
	clock_t st=0;
	PF("applyexpr (code, xarg, yarg):\n");DUMP(code);DUMP(xarg);DUMP(yarg);
	if(!LIST(code))return EXC(Tt(code),"expr code not list",code,xarg);
	left=xarg; if(!yarg) yused=1;
	tcom=Ti(comment); texc=Ti(exception); tlam=Ti(lambda); 
	traw=Ti(raw); tstr=Ti(string); tws=Ti(ws);
	if(!LIST(code)) code=list(code);
	for(i=0;i<code->n;i++) {
		PF("applyexpr #%d/%d, consumed=%d/%d\n",i,code->n-1,xused,yused);
		DUMP(left);
		item = ELl(code,i);
		DUMP(item);
		// consider storing these skip conditions in an array
		if(LIKELY(IS_c(item))) {
			ch = AS_c(item,0);
			if(item->tag == traw && ch=='{')
				// big debate in my head if we should catch and silently ignore some of
				// these markers characters here inside applyexpr(). obviously it would
				// be faster and cleaner to remove these from the input parse tree as we
				// parse it (see 'parseexpr' below in the parse() area) but considering
				// the need to go 'deep' on them im not sure, as for deep parse trees
				// this could be very slow. perhaps nest() itself should have an option
				// to drop the brackets? seems hacky, but only nest() knows the grouping
				// boundaries without further calc
				continue;
			else if(item->tag == traw && ch=='}')
				continue;
			else if(item->tag == traw && ch=='(')
				continue;
			else if(item->tag == traw && ch==')')
				continue;
			else if(item->tag == traw && ch==';') { // end of expr indicator
				left=0;
				continue;
			}
			else if(item->tag == tcom) 
				continue;
			else if(item->tag == tws)
				// skip ws
				continue;
			else if(item->tag != tstr) {
				PF("much ado about\n");DUMP(item);
				if(item->n==1 && ch=='x')
					item=xarg;
				else if(item->n==1 && ch=='y' && yarg!=0) {
					PF("picking up y arg\n");DUMP(yarg);
					item=yarg;
				}
				else if(item->n==2 && ch=='a' && AS_c(item,1)=='s') {
					left=mkproj(2,&set,xln(2,parent,left),0);
					xfree(left);
					PF("created set projection\n");
					DUMP(left);
					continue;
				} else if(item->n==2 && ch=='.' && AS_c(item,1)=='t') {
					printf("timer on\n");
					st=clock();
					continue;
				} else
					item=get(parent,item);
				PF("decoded string identifier\n");DUMP(item);
				if(item->tag==texc)
					return CALLABLE(left)?left:item;

			}
		}
		
		//if(LIST(item))
		//	left=applyexpr(parent,item,left);
		if(item->tag==tlam) { // create a context for lambdas
			VP newctx,this; int j;
			newctx=xx0();
			item=list(item);
			for(j=0;j<parent->n;j++) {
				this=ELl(parent,j);
				if(j!=parent->n-1 || !LIST(this))
					append(newctx,ELl(parent,j));
			}
			append(newctx,entags(ELl(item,0),"")); // second item of lambda is arity; ignore for now
			item=newctx;
			PF("created new lambda context item=\n");DUMP(item);
		} else if(LIST(item)) {
			PF("applying subexpression\n");
			item=applyexpr(parent,item,left,!yused?yarg:0);
			PF("subexpression came back with");DUMP(item);
		}

		if(xused && left!=0 && CALLABLE(left)) {
			// they seem to be trying to call a unary function, though it's on the
			// left - NB. possibly shady
			//
			// if you pass a projection as the xargument, we should NOT immediately pass
			// the next value to it, even though it appears as "left". xused acts
			// as a gate for that "are we still possibly needing the passed-in (left) value?"
			// logic to not allow this behavior in first position inside expression
			Proj p;
			left=apply(left,item);
			if(IS_p(left)) {
				p=AS_p(left,0);
				if(!yused && p.type==2 && (!p.left || !p.right)) {
					PF("applyexpr consuming y:\n");DUMP(yarg);
					left=apply(left,yarg);
					yused=1;
				}
			}
		} else if(SIMPLE(item) || LIST(item)) {
			PF("applyexpr adopting left =\n");DUMP(item);
			xused=1;
			left=item;
		} else {
			PF("applyexpr calling apply\n");DUMP(item);DUMP(left);
			xused=1;
			PF("111\n");
			left=apply(item,left);
			if(left->tag==texc) return left;
			PF("applyexpr apply returned\n");DUMP(left);
		}
	}
	PF("applyexpr returning\n");
	DUMP(left);
	if(st!=0) {
		clock_t en;
		en = clock();
		printf("time=%0.4f\n", ((double)(en-st)) / CLOCKS_PER_SEC); 
	}
	return left;
}
VP applyctx(VP x,VP y) {
	// structure of a context. like a general list conforming to:
	// first item: code. list of projections or values. evaluated left to right.
	// second item: parent context. used to resolve unidentifiable symbols
	// third item: parent's parent
	// .. and so on
	// checked in order from top to bottom with apply()
	if(!IS_x(x)) return EXC(Tt(type),"context not a context",x,y);
	int i;VP this,res=NULL;
	PF("applyctx\n");DUMP(x);DUMP(y);
	for(i=x->n-1;i>0;i--) {
		this=ELl(x,i);
		PF("applyctx #%d\n", i);
		DUMP(this);
		if(LIST(this)) { // code bodies are lists - maybe use 'code tag instead? 
			PF("CTX CODE BODY\n");DUMP(this);
			res=applyexpr(x,this,y,0);
		}
		// NB. if the function body returns an empty list, we try the scopes (dictionaries).
		// this may not be what we want in the long run.
		if(res==NULL || (LIST(res) && res->n == 0))
			res=apply(this,y);
		if(!LIST(res) || res->n > 0) {
			PF("applyctx returning\n"); DUMP(res); 
			return res;
		}
	}
	return EXC(Tt(undef),"undefined in applyctx",x,y);
	/*
	first=ELl(x,0);
	if(!IS_d(first) || x->n < 2) return EXC(Tt(noctx),"context without locals",x,y);
	left=y;
	if(first->t==0) {
		left=applyexpr(ELl(x,1),first,left);
	} else {
		for(i=0;i<x->n;i++) {
			left=apply(ELl(x,i),y);
			if(!LIST(left) || left->n > 0)
				break;
		}
	}
	PF("applyctx returning\n");DUMP(left);
	return left;
	*/
}
VP apply(VP x,VP y) {
	// this function is everything.
	VP res=NULL;int i,typerr=-1;
	// PF("apply\n");DUMP(x);DUMP(y);
	if(x->tag==_tagnums("exception"))return x;
	if(DICT(x)) {
		VP k=KEYS(x),v=VALS(x);I8 found;
		if(k==NULL || v==NULL) return NULL;
		res=xi0();
		if(LIST(k) && IS_c(y)) { // special case for strings as dictionary keys - common
			int idx;
			PF("searching for string\n");
			if ((idx=_find1(k,y))>-1) {
				// PF("string found at idx %d\n", idx);
				append(res,xi(idx));
			}
		} else {
			ITERV(y,{ 
				int idx;
				// PF("searching %d\n",_i);
				// DUMP(y); DUMP(k);
				if(LIST(y)) idx = _find1(k,ELl(y,_i));
				else idx = _findbuf(k,ELi(y,_i));
				if(idx>-1) {
					found=1;
					PF("found at idx %d\n", idx); 
					append(res,xi(idx));
					break;
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
		// PF("apply proj\n");DUMP(x);
		Proj* p; p=(Proj*)ELi(x,0);
		// if(!p->left) p->left=y; else if (!p->right) p->right=y;
		// DUMP(p->left); DUMP(p->right);
		if(p->type==1) {
			if (p->left) // not sure if this will actually happen - seems illogical
				return (*p->f1)(p->left);
			else
				return (*p->f1)(y);
		}
		if(p->type==2) {
			PF("proj2\n"); 
			if(!y) return x;
			else {
				PFIN();
				if(p->left)
					y=(*p->f2)(p->left, y);
				else if (p->right)
					y=(*p->f2)(y,p->right);
				else
					y=mkproj(2,p->f2,y,0);
				PFOUT(); PF("proj2 done\n"); DUMP(y);
				return y;
			}
		}
		return xp(*p);
	}
	if(IS_1(x)) {
		PF("apply 1\n");DUMP(x);DUMP(y);
		unaryFunc* f; f=AS_1(x,0); return (*f)(y);
	}
	if(IS_2(x)) {
		res=mkproj(2,AS_2(x,0),y,0);
		PF("apply f2 returning\n");DUMP(res);
		return res;
	}
	if(IS_x(x)) {
		// PF("apply ctx\n");DUMP(x);DUMP(y);
		return applyctx(x,y);
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
	return EXC(Tt(apply),"cant apply that",x,y);
}
VP deep(VP obj,VP f) {
	// TODO perhaps deep() should throw an error with non-list args - calls each() now
	int i;
	// PF("deep\n");DUMP(obj);DUMP(f);
	VP acc,subobj;
	if(!LIST(obj)) return each(obj,f);
	if(_flat(obj)) {
		PF("deep flat\n");
		acc=apply(f,obj);
		if(obj->tag) acc->tag=obj->tag;
		return acc;
	}
	acc=xl0();
	if(obj->tag) acc->tag=obj->tag;
	PFIN();
	FOR(0,obj->n,({
		// PF("deep %d\n", _i);
		subobj=ELl(obj,_i);
		if(LIST(subobj))
			subobj=deep(subobj,f);
		else
			subobj=apply(f,subobj);
		append(acc,subobj);
	}));
	PFOUT();
	// PF("deep returning\n");DUMP(acc);
	return acc;
}
static inline VP each(VP obj,VP fun) { 
	// each returns a list if the first returned value is the same as obj's type
	// and has one item
	VP tmp, res, acc=NULL; int n=obj->n;
	// PF("each\n");DUMP(obj);DUMP(fun);
	FOR(0,n,({ 
		// PF("each #%d\n",n);
		tmp=apply(obj, xi(_i)); res=apply(fun,tmp); 
		// delay creating return type until we know what this func produces
		if (!acc) acc=xalloc(SCALAR(res) ? res->t : 0,obj->n); 
		else if (!LIST(acc) && res->t != acc->t) 
			acc = xl(acc);
		xfree(tmp);
		append(acc,res); }));
	// PF("each returning\n");DUMP(acc);
	return acc;
}
static inline VP eachprior(VP obj,VP fun) {
	ASSERT(1,"eachprior nyi");
	return (VP)0;
}
VP exhaust(VP x,VP y) {
	int i;
	PF("+++EXHAUST\n");DUMP(x);DUMP(y);
	IF_RET(CALLABLE(x), EXC(Tt(type),"exhaust y must be func or projection",x,y));
	IF_RET(x->n==0, ALLOC_LIKE_SZ(x,0));
	VP last=x,this=0;
	for(i=0;i<MAXSTACK;i++) {
		PF("exhaust calling #%d\n",i);DUMP(y);DUMP(this);DUMP(last);
		PFIN();
		this=apply(y,last);
		PFOUT();
		PF("exhaust result #%d\n",i);DUMP(last);DUMP(this);
		if(UNLIKELY(_equal(this,last))) {
			PF("exhaust = returning\n");DUMP(this);
			return this;
		} else 
			last=this;
	}
	return EXC(Tt(exhausted),"exhaust hit stack limit",x,last);
}
VP over(VP x,VP y) {
	PF("over\n");DUMP(x);DUMP(y);
	IF_RET(!CALLABLE(y), EXC(Tt(type),"over y must be func or projection",x,y));
	IF_RET(x->n==0, xalloc(x->t, 0));
	VP last,next;
	last=apply(x,xi(0));
	FOR(1,x->n,({
		next=apply(x, xi(_i));
		last=apply(apply(y,last),next);
	}));
	return last;
}
VP scan(VP x,VP y) { // always returns a list
	PF("scan\n");DUMP(x);DUMP(y);
	IF_RET(!CALLABLE(y), EXC(Tt(type),"scan y must be func or projection",x,y));
	IF_RET(x->n==0, xalloc(x->t, 0));
	IF_RET(x->n==1, x);
	VP last,next,acc=0;
	last=apply(x,xi(0));
	acc=ALLOC_LIKE(x);
	append(acc,last);
	FOR(1,x->n,({
		next=apply(x, xi(_i));
		last=apply(apply(y,last),next);
		PF("scan step\n");DUMP(last);
		append(acc,last);
	}));
	PF("scan result\n");DUMP(acc);
	return acc;
}

// MATHY STUFF:

VP and(VP x,VP y) {
	int typerr=-1;
	VP acc;
	// PF("and\n"); DUMP(x); DUMP(y); // TODO and() and friends should handle type conversion better
	IF_EXC(x->n > 1 && y->n > 1 && x->n != y->n, Tt(len), "and arguments should be same length", x, y);	
	if(x->t == y->t) acc=xalloc(x->t, x->n);
	else acc=xlsz(x->n);
	VARY_EACHBOTH(x,y,({ 
		if (_x < _y) appendbuf(acc, (buf_t)&_x, 1); 
		else appendbuf(acc, (buf_t)&_y, 1); }), typerr);
	IF_EXC(typerr != -1, Tt(type), "and arg type not valid", x, y);
	// PF("and result\n"); DUMP(acc);
	return acc;
}
int _any(VP x) {
	int typerr=-1;
	VP acc;
	// PF("_any\n"); DUMP(x);
	if(LIST(x)) x=deep(x,x1(&any));
	VARY_EACH(x,({ 
		if(_x==1) return 1;
	}),typerr);
	// since this routine returns an int we can't return an exception!
	if(typerr>-1)return EXC(Tt(type),"_arg noniterable x",x,0);
	ASSERT(typerr==-1,"_any() non-iterable type");
	PF("_any returning 0\n");
	return 0;
}
VP any(VP x) {
	//PF("any\n");DUMP(x);
	IF_EXC(!SIMPLE(x) && !LIST(x), Tt(type), "any arg must be list or simple type", x, 0);
	if(LIST(x)) return deep(x,x1(&any));
	return xb(_any(x));
}
VP greater(VP x,VP y) {
	int typerr=-1;
	VP acc,v0=xb(0),v1=xb(1);
	PF("greater\n"); DUMP(x); DUMP(y); 
	if(!SIMPLE(x) || !SIMPLE(y)) return EXC(Tt(type), "> args should be simple types", x, y);
	IF_EXC(x->n > 1 && y->n > 1 && x->n != y->n, Tt(len), "> arguments should be same length", x, y);	
	acc=xbsz(MAX(x->n,y->n));
	VARY_EACHBOTH(x,y,({ 
		if (_x <= _y) append(acc, v0); 
		else append(acc, v1); 
		if(!SCALAR(x) && SCALAR(y)) _j=-1; // NB. AWFUL!
	}), typerr);
	IF_EXC(typerr != -1, Tt(type), "> arg type not valid", x, y);
	PF("> result\n"); DUMP(acc);
	xfree(v0);xfree(v1);
	return acc;
}
VP lesser(VP x,VP y) {
	int typerr=-1;
	VP acc,v0=xb(0),v1=xb(1);
	PF("lesser\n"); DUMP(x); DUMP(y); 
	if(!SIMPLE(x) || !SIMPLE(y)) return EXC(Tt(type), "< args should be simple types", x, y);
	IF_EXC(x->n > 1 && y->n > 1 && x->n != y->n, Tt(len), "< arguments should be same length", x, y);	
	acc=xbsz(MAX(x->n,y->n));
	VARY_EACHBOTH(x,y,({ 
		if (_x >= _y) append(acc, v0); 
		else append(acc, v1); 
		if(!SCALAR(x) && SCALAR(y)) _j=-1; // NB. AWFUL!
	}), typerr);
	IF_EXC(typerr != -1, Tt(type), "< arg type not valid", x, y);
	PF("< result\n"); DUMP(acc);
	xfree(v0);xfree(v1);
	return acc;
}
VP or(VP x,VP y) { // TODO most of these primitive functions have the same pattern - abstract?
	int typerr=-1;
	VP acc;
	// PF("or\n"); DUMP(x); DUMP(y); // TODO or() and friends should handle type conversion better
	IF_EXC(x->n > 1 && y->n > 1 && x->n != y->n, Tt(len), "or arguments should be same length", x, y);	
	if(x->t == y->t) acc=xalloc(x->t, x->n);
	else acc=xlsz(x->n);
	VARY_EACHBOTH(x,y,({ if (_x > _y) appendbuf(acc, (buf_t)&_x, 1); 
		else appendbuf(acc, (buf_t)&_y, 1); }), typerr);
	IF_EXC(typerr != -1, Tt(type), "or arg type not valid", x, y);
	// PF("or result\n"); DUMP(acc);
	return acc;
}
VP min(VP x) { 
	return over(x, x2(&and));
}
VP max(VP x) { 
	return over(x, x2(&or));
}
VP mod(VP x,VP y) {
	// TODO mod probably *doesnt* need type promotion
	int typerr=-1;
	PF("mod\n");DUMP(x);DUMP(y);
	IF_EXC(!SIMPLE(x) || !SIMPLE(y), Tt(type), "mod args should be simple types", x, y); 
	VP acc=ALLOC_BEST(x,y);
	if(LIKELY(x->t > y->t)) {
		VARY_EACHBOTH(x,y,({ 
			_x=_y%_x; appendbuf(acc,(buf_t)&_x,1); if(!SCALAR(x) && SCALAR(y)) _j=-1; // NB. AWFUL!
		}),typerr);
	} else {
		VARY_EACHBOTH(x,y,({ 
			_y=_y%_x; appendbuf(acc,(buf_t)&_y,1); if(!SCALAR(x) && SCALAR(y)) _j=-1; // NB. AWFUL!
		}),typerr);
	}
	IF_EXC(typerr > -1, Tt(type), "mod arg wrong type", x, y);
	PF("mod result\n"); DUMP(acc);
	return acc;
}
VP plus(VP x,VP y) {
	int typerr=-1;
	PF("plus\n");DUMP(x);DUMP(y);
	IF_EXC(!SIMPLE(x) || !SIMPLE(y), Tt(type), "plus args should be simple types", x, y); 
	VP acc=ALLOC_BEST(x,y);
	VARY_EACHBOTH(x,y,({
		if(LIKELY(x->t > y->t)) { _x=_x+_y; appendbuf(acc,(buf_t)&_x,1); }
		else { _y=_y+_x; appendbuf(acc,(buf_t)&_y,1); }
		if(!SCALAR(x) && SCALAR(y)) _j=-1; // NB. AWFUL!
	}),typerr);
	IF_EXC(typerr > -1, Tt(type), "plus arg wrong type", x, y);
	PF("plus result\n"); DUMP(acc);
	return acc;
}
VP str2int(VP x) {
	I128 buf=0;
	PF("str2int %s",sfromx(x));DUMP(x);
	IF_EXC(!IS_c(x),Tt(type),"str2int arg should be char vector",x,0);
	if (sscanf(sfromx(x),"%lld",&buf)==1) {
		PF("inner %lld\n",buf);
		/* assume int by default 
		if(buf<MAX_b)
			return xb((CTYPE_b)buf);
		*/
		if(buf<MAX_i)
			return xi((CTYPE_i)buf);
		if(buf<MAX_j)
			return xj((CTYPE_j)buf);
		if(buf<MAX_o)
			return xo((CTYPE_o)buf);
	}
	return EXC(Tt(value),"str2int value could not be converted",x,0);
}
VP sum(VP x) {
	PF("sum");DUMP(x);
	I64 val=0;int i,typerr=-1;
	if(UNLIKELY(!SIMPLE(x))) return EXC(Tt(type),"sum argument should be simple types",x,0);
	VARY_EACH(x,({ val += _x; }),typerr);
	IF_EXC(typerr > -1, Tt(type), "sum arg wrong type", x, 0);
	return xj(val);
}
VP sums(VP x) {
	PF("sums\n");DUMP(x);
	if(UNLIKELY(!SIMPLE(x))) return EXC(Tt(type),"sums argument should be simple types",x,0);
	VP acc=ALLOC_LIKE(x); int typerr=-1;
	VARY_EACH(x,({ _xtmp += _x; appendbuf(acc,(buf_t)&_xtmp,1); }),typerr);
	IF_EXC(typerr > -1, Tt(type), "sums arg wrong type", x, 0);
	PF("sums result\n"); DUMP(acc);
	return acc;
}
VP til(VP x) {
	VP acc=0;int i;int typerr=-1;
	PF("til\n"); DUMP(x);
	VARY_EL(x, 0, 
		{ __typeof__(_x) i; acc=xalloc(x->t,MAX(_x,1)); acc->n=_x; for(i=0;i<_x;i++) { EL(acc,__typeof__(i),i)=i; } }, 
		typerr);
	IF_RET(typerr>-1, EXC(Tt(type), "til arg must be numeric", x, 0));
	DUMP(acc);
	return acc;
}
static inline VP times(VP x,VP y) {
	int typerr=-1; VP acc=ALLOC_BEST(x,y);
	PF("times");DUMP(x);DUMP(y);DUMP(info(acc));
	if(UNLIKELY(!SIMPLE(x))) return EXC(Tt(type),"times argument should be simple types",x,0);
	VARY_EACHBOTH(x,y,({
		if(LIKELY(x->t > y->t)) { _x=_x*_y; appendbuf(acc,(buf_t)&_x,1); }
		else { _y=_y*_x; appendbuf(acc,(buf_t)&_y,1); }
		if(!SCALAR(x) && SCALAR(y)) _j=-1; // NB. AWFUL!
	}),typerr);
	IF_EXC(typerr > -1, Tt(type), "times arg wrong type", x, y);
	PF("times result\n"); DUMP(acc);
	return acc;
}
VP xor(VP x,VP y) { 
	int typerr=-1;
	VP acc;
	PF("xor\n"); DUMP(x); DUMP(y); // TODO or() and friends should handle type conversion better
	if(UNLIKELY(!SIMPLE(x) && !SIMPLE(y))) return EXC(Tt(type),"xor argument should be simple types",x,0);
	IF_EXC(x->n > 1 && y->n > 1 && x->n != y->n, Tt(len), "xor arguments should be same length", x, y);	
	if(x->t == y->t) acc=xalloc(x->t, x->n);
	else acc=xlsz(x->n);
	VARY_EACHBOTH(x,y,({ _x = _x ^ _y; appendbuf(acc, (buf_t)&_x, 1); }), typerr);
	IF_EXC(typerr != -1, Tt(type), "xor arg type not valid", x, y);
	// PF("or result\n"); DUMP(acc);
	return acc;
}

// MANIPULATING LISTS AND VECTORS (misc):

VP get(VP x,VP y) {
	// TODO get support nesting
	int i; VP res;
	PF("get\n");DUMP(y);
	if(IS_x(x)) {
		if(IS_c(y)) {
			res=xt(_tagnum(y));
			xfree(y);
			y=res;
		}
		i=x->n-1;
		for(;i>=0;i--) {
			PF("get #%d\n", i);
			if(LIST(ELl(x,i)))
				continue;
			res=apply(ELl(x,i),y);
			if(!LIST(res) || res->n > 0) {
				// PF("get returning\n");DUMP(res);
				return res;
			}
		}
		return EXC(Tt(undef),"undefined",y,x);
	}
	return apply(x,y);
}

VP set(VP x,VP y) {
	// TODO set needs to support nesting
	int i; VP res,ctx,val;
	PF("set\n");DUMP(x);DUMP(y);
	if(LIST(x)) {
		if(!IS_x(AS_l(x,0))) return EXC(Tt(type),"set x must be (context,value)",x,y);
		if(x->n!=2) return EXC(Tt(type),"set x must be (context,value)",x,y);
		if(!IS_t(y)) return EXC(Tt(type),"set y must be symbol",x,y);
		ctx=AS_l(x,0);val=AS_l(x,1); i=ctx->n-1;
		for(;i>=0;i--) {
			printf("set %d\n", i);
			VP this = AS_x(ctx,i);
			DUMP(this);
			if(LIST(this) || CALLABLE(this)) // skip code bodies
				continue;
			if(DICT(this)) {
				PF("set assigning..\n");
				this=assign(this,y,val);
				PF("set in %p\n", this);
				DUMP(this);
				return val;
			}
		}
		return EXC(Tt(set),"could not set value in parent scope",x,y);
	}
	return xl0();
}

VP partgroups(VP x) { 
	// separate 1 3 4 5 7 8 -> [1, 3 4 5, 7 8]; always returns a list, even with one item
	VP acc,tmp;int n=0,typerr=-1;
	PF("partgroups\n");DUMP(x);
	acc=xlsz(x->n/2);
	tmp=xalloc(x->t,4);
	VARY_EACHLIST(x,({
		if(_i==0) {
			_xtmp=_x;
		} else {
			// PF("pg inner %d %d\n", _xtmp, _x);
			if(ABS(_xtmp-_x) != 1) {
				acc=append(acc,tmp);
				xfree(tmp);
				tmp=xalloc(x->t,4);
			} 
			_xtmp=_x;
		}
		tmp=appendbuf(tmp,(buf_t)&_x,1);
	}),typerr);
	IF_EXC(typerr>-1, Tt(type), "partgroups args should be simple types", x, 0); 
	if(tmp->n) append(acc,tmp);
	PF("partgroups returning\n");DUMP(acc);
	xfree(tmp);
	return acc;
}

VP pick(VP x,VP y) { // select items of x[0..n] where y[n]=1
	VP acc;int n=0,typerr=-1;
	IF_EXC(!SIMPLE(x) || !SIMPLE(y), Tt(type), "pick args should be simple types", x, y); // TODO pick: gen lists
	PF("pick\n");DUMP(x);DUMP(y);
	acc=ALLOC_LIKE_SZ(x,x->n/2);
	VARY_EACHBOTHLIST(x,y,({
		if(_y) {
			// PF("p %d %d/%d %d %d/%d s%d\n", _x,_i,_xn,_y,_j,_yn,SCALAR(x));
			acc=appendbuf(acc,(buf_t)&_x,1);
			n++;
		}
	}),typerr);
	IF_EXC(typerr > -1, Tt(type), "pick arg wrong type", x, y);
	PF("pick result\n"); DUMP(acc);
	return acc;
}
VP pickapart(VP x,VP y) { // select items of x[0..n] where y[n]=1, and divide non-consecutive regions
	VP acc, sub=NULL;int n=0,typerr=-1;
	IF_EXC(!SIMPLE(x) || !SIMPLE(y), Tt(type), "pickapart args should be simple types", x, y); // TODO pick: gen lists
	PF("pickapart\n");DUMP(x);DUMP(y);
	acc=xlsz(4);
	VARY_EACHBOTHLIST(x,y,({
		PF("%d ",_y);
		if(_y) {
			if (!sub) sub=ALLOC_LIKE_SZ(x,x->n/2);
			sub=appendbuf(sub,(buf_t)&_x,1);
		} else {
			if (sub) { acc=append(acc,sub); xfree(sub); sub=NULL; }
		}
	}),typerr);
	IF_EXC(typerr > -1, Tt(type), "pickapart arg wrong type", x, y);
	if (sub) { acc=append(acc,sub); xfree(sub); sub=NULL; }
	PF("pickapart result\n"); DUMP(acc);
	if(acc->n==0) return ELl(acc,0);
	else return acc;
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
VP name2sym(VP x) {
	PF("name2sym\n");DUMP(x);
	if(IS_c(x) && AS_c(x,0)=='\'') {
		return xt(_tagnum(drop_(x,1)));
	} 
	else return xl0();
}

// TAG STUFF:

static inline VP tagwrap(VP tag,VP x) {
	return entag(xln(1, x),tag);
}
static inline VP tagv(const char* name, VP x) {
	return entags(xln(1,x),name);
}
static inline VP entag(VP x,VP t) {
	if(IS_c(t))
		x->tag=_tagnum(t);
	else if (IS_i(t))
		x->tag=AS_i(t,0);
	return x;
}
static inline VP entags(VP x,const char* name) {
	x->tag=_tagnums(name);
	return x;
}
static inline VP tagname(const I32 tag) {
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
static inline const char* tagnames(const I32 tag) {
	return sfromx(tagname(tag));
}
static inline int _tagnum(const VP s) {
	int i; VP ss=0;
	WITHLOCK(tag, {
		ss=s;ss->tag=0;
		if(TAGS==NULL) { TAGS=xl0();TAGS->rc=INT_MAX;upsert(TAGS,xfroms("")); PF("new tags\n"); DUMP(TAGS); }
		i=_upsertidx(TAGS,s);
	});
	// PF("tagnum %s -> %d\n",name,i);
	// DUMP(TAGS);
	return i;
}
static inline int _tagnums(const char* name) {
	int t;VP s;
	//printf("_tagnums %s\n",name);
	//printf("tagnums free\n");
	// DUMP(TAGS);
	s=xfroms(name); t=_tagnum(s); xfree(s); return t;
}

// JOINS (so to speak)
// possibly useless

VP bracketjoin(VP x,VP y) { 
	// returns x[n] when 'on'
	//  turned on by y[0][n]=1
	//  turned off by y[1][n]=1
	// otherwise 0
	// useful for matching patterns involving more than one entity
	int i,on=0,typerr=-1; VP c,ret,acc,y0,y1,mx;
	PF("bracketjoin\n");DUMP(x);DUMP(y);
	IF_EXC(!LIST(y)||y->n!=2,Tt(type),"bracketjoin y must be 2-arg list",x,y);
	y0=ELl(y,0); y1=ELl(y,1);
	IF_EXC(y0->t != y1->t,Tt(type),"bracketjoin y items must be same type",x,y);
	acc=plus(y0, times(xi(-1),y1));
	DUMP(acc);
	acc=sums(acc);
	PF("bracket sums\n");DUMP(acc);
	mx=max(acc);
	PF("bracket max\n");DUMP(mx);
	PF("bracket x\n");DUMP(x);
	ret=take(xi(0),xi(y0->n));
	if(EL(mx,CTYPE_b,0)==0) { PF("bracketjoin no coverage\n"); DUMP(acc); return ret; }
	c=ELl(partgroups(condense(and(x,matcheasy(acc,mx)))),0);
	DUMP(c);
	if(c->n) {
		c=append(c,plus(max(c),xi(1)));
		PF("bracket append next\n");
		DUMP(c);
	}
	ret=assign(ret,c,xi(1));
	// acc=pick(x,matcheasy(acc,mx));
	PF("bracket acc after pick");DUMP(ret);
	return ret;
	acc = ALLOC_LIKE_SZ(x,y0->n);
	VARY_EACHRIGHT(x,y0,({
		if(_y == 1) on++;
		if(on) EL(acc,typeof(_x),_j)=EL(x,typeof(_x),_j % x->n);
		else EL(acc,typeof(_x),_j)=0;
		// NB. the off channel affects the *next element*, not this one - maybe not right logic
		if(_j < y1->n && EL(y1,typeof(_y),_j)==1) on--;
	}),typerr);
	IF_EXC(typerr>-1,Tt(type),"bracketjoin couldnt work with those types",x,y);
	acc->n=y0->n;
	mx=max(acc);
	PF("bracketjoin max\n");DUMP(mx);
	acc=matcheasy(acc,mx);
	PF("bracketjoin return\n");DUMP(acc);
	return acc;
}
VP consecutivejoin(VP x, VP y) {
	// returns x[n] if y[0][n]==1 && y[1][n+1]==1 && .. else 0
	int j,n=y->n, typerr=-1, on=0; VP acc,tmp;
	PF("consecutivejoin\n"); DUMP(x); DUMP(y);
	
	if(!LIST(y)) return and(x,y);

	IF_EXC(!LIST(y)||y->n<1,Tt(type),"consecutivejoin y must be list of simple types",x,y);
	VP y0=ELl(y,0);
	for(j=0; tmp=ELl(y,j), j<n; j++) 
		IF_EXC(tmp->t!=y0->t,Tt(type),"consecutivejoin y must all be same type or similar numeric",x,y);
	acc = ALLOC_LIKE_SZ(x,y0->n);
	VARY_EACHRIGHT(x,y0,({
		if(UNLIKELY(_y)==1) {
			on=1;
			for(j=0; tmp=ELl(y,j), j<n; j++) {
				if(_j + j > tmp->n || EL(tmp,typeof(_y),_j+j) == 0){on = 0; break;}
			} 
			if(on) EL(acc,typeof(_x),_j)=EL(x,typeof(_x),_j % x->n);
			else EL(acc,typeof(_x),_j)=0; 
		}
	}),typerr);
	IF_EXC(typerr>-1,Tt(type),"consecutivejoin couldnt work with those types",x,y);
	acc->n=y0->n;
	PF("consecutivejoin return\n"); DUMP(acc);
	return acc;
}
VP signaljoin(VP x,VP y) {
	// could be implemented as +over and more selection but this should be faster
	int typerr=-1, on=0; VP acc;
	PF("signaljoin\n");DUMP(x);DUMP(y);
	acc = ALLOC_LIKE_SZ(x,y->n);
	if(SCALAR(x)) { // TODO signaljoin() should use take to duplicate scalar args.. but take be broke
		VARY_EACHRIGHT(x,y, ({
			if(_y == 1) on=!on;
			if(on) EL(acc,typeof(_x),_j)=(typeof(_x))_x;
			else EL(acc,typeof(_x),_j)=0;
		}), typerr);
	} else {
		VARY_EACHBOTHLIST(x,y,({
			if(_y == 1) on=!on;
			if(on) EL(acc,typeof(_x),_i)=(typeof(_x))_x;
			else EL(acc,typeof(_x),_i)=0;
		}),typerr);
	}
	acc->n=y->n;
	IF_EXC(typerr>-1,Tt(type),"signaljoin couldnt work with those types",x,y);
	PF("signaljoin return\n");DUMP(acc);
	return acc;
}

// MATCHING

VP nest(VP x,VP y) {
	/* 
		nest() benchmarking: (based on 20k loops of test_nest())
		_equalm (single-value delimeter), with debugging: ~12.5s
		_equalm, without debugging: 3.2s
	*/
	VP p1,p2,open,close,opens,closes,where,rep,out;
	PF("NEST\n");DUMP(x);DUMP(y);
	/*
	if(LIST(x)) {
		PFW({
		x=deep(x,mkproj(2,&nest,0,y));
		});
	}
	*/
	p1=mkproj(2,&matcheasy,x,0);
	p2=mkproj(2,&matcheasy,x,0);
	open=apply(y,xi(0)); close=apply(y,xi(1));
	if(_equal(open,close)) {
		opens=each(open,p1);
		PF("+ matching opens\n");DUMP(opens);
		/*
		 * out=scan(AS_l(opens,0),x2(&xor));
		DUMP(out);
		out=scan(AS_l(opens,0),x2(&xor));
		*/
		if(_any(opens)) {
			opens=signaljoin(xb(1),AS_l(opens,0));
			PF("after signaljoin\n");DUMP(opens);
			out=partgroups(condense(opens));
			if(out->n) {
				out=AS_l(out,0);
				PF("matching pre-append"); DUMP(out);
				out=append(out,plus(max(out),xi(1)));
				PF("matching post-append"); DUMP(out);
			}
			DUMP(out);
			where=out;
		} else 
			where=xl0();
		// exit(1);
	} else {
		opens=each(open,p1);
		PF("+ opens\n");DUMP(opens);
		closes=each(close,p2);
		PF("- closes\n");DUMP(closes);
		if(LIST(opens)) {
			if (!AS_b(any(AS_l(opens,0)),0)) return x;
		} else if(!AS_b(any(opens),0)) return x;
		if(LIST(closes)) {
			if (!AS_b(any(AS_l(closes,0)),0)) return x;
		} else if(!AS_b(any(closes),0)) return x;
		// remember that bracket join needs the farthest-right
		// index of the matching closing bracket, if it's more than
		// one item
		closes=consecutivejoin(xb(1),closes);
		if(close->n > 1)
			closes=shift_(closes, (close->n-1)*-1);
		out=bracketjoin(xb(1), xln(2,consecutivejoin(xb(1),opens),closes)); 
		DUMP(out);
		where=condense(out);
	}
	PF("nest where\n");DUMP(where);
	//out=splice(x,out,xl(apply(x,out)));
	if(where->n) {
		//out=splice(x,out,split(apply(x,out),xi0()));
		rep=apply(x,where);
		if(y->n >= 4)
			rep->tag=AS_t(ELl(y,3),0);
		rep=list2vec(rep);
		PF("nest rep\n");DUMP(rep);
		// splice is smart enough to merge like-type replace args into one
		// like-typed vector. but that's not what we want here, because the
		// thing we're inserting is a "child" of this position, so we want to
		// ensure we always splice in a list
		
		//if(!LIST(rep)) rep=xl(rep);     up for debate

		PF("nest x");
		out=splice(split(x,xi0()),where,rep);
		PF("nest out\n");DUMP(out);
		if(!LIST(out)) out=xl(out);
	} else { out = x; }
	//if(ENLISTED(out))out=ELl(out,0);
	PF("nest returning\n"); DUMP(out);
	return out;

	/*
	int i,j,found; 
	VP tmp, this, open, close, escape, entag, st, cur, newcur; // result stack
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
			for(j=0; j< open->n && i+j < x->n; j++) { // too confusing to express with FOR
				PF("copy %d, i=%d\n", j, i);
				tmp=apply(x,xi(i++));
				if(entag!=NULL)
					tmp->tag=0;
				append(cur, tmp);
			}
			i--;
			PF("nest start done copying");
			DUMP(cur);
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
				found++;
			}
		}  else {
			// TODO need an apply shortcut for C types like applyi(VP,int) - too slow otherwise
			newcur=apply(x,xi(i));
			PF("nest appending elem %d\n", i);
			DUMP(newcur);
			if(entag!=NULL) 
				newcur->tag=0;
			append(cur,newcur);
		}
	}
	if (found) {
		PF("nest returning new value");
		DUMP(st);
		return st;
	} else {
		PF("returning unchanged");
		return x;
	}
	*/
}

static inline void matchanyof_(const VP obj,const VP pat,const int max_match,int* n_matched,int* matchidx) {
	int i=0,j=0;VP item,rule,tmp=xi(0);
	int submatches;
	int submatchidx[1024];
	for(;i<obj->n;j++) {
		for(;i<pat->n;i++) {
			EL(tmp,int,0)=i;
			item=apply(obj,tmp);
			rule=apply(obj,tmp);
			xfree(item);
		}
	}
}

static inline int match_(const VP obj_,int ostart, const VP pat_, int pstart, 
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
				PF("!!!! NEW IO %d\n", io);
				*n_matched=submatches-1;
				DUMP(item);
				DUMP(rule);
			}
			goto done;
		}

		if(IS_t(rule)) {
			if((AS_t(rule,0) != item->tag && AS_t(rule,0) != obj->tag) ||
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
	PF("match() obj, pat, and result:\n"); DUMP(obj); DUMP(pat); DUMP(acc);
	return acc;
}
VP matchany(VP obj,VP pat) {
	IF_EXC(!SIMPLE(obj) && !LIST(obj),Tt(type),"matchany only works with simple or list types in x",obj,pat);
	// IF_EXC(!SIMPLE(pat),Tt(type),"matchany only works with simple types in y",obj,pat);
	IF_EXC(SIMPLE(obj) && obj->t != pat->t, Tt(type),"matchany only works with matching simple types",obj,pat);
	int j,n=obj->n,typerr=-1;VP item, acc;
	PF("matchany\n"); DUMP(obj); DUMP(pat);
	acc=xbsz(n); 
	acc->n=n;
	if(LIST(obj)) {
		VP this;
		FOR(0,n,({ 
			this=ELl(obj,_i);
			if((pat->tag==0 || pat->tag==this->tag) && _find1(pat,this) != -1) {
				PF("matchany found list at %d\n", _i);
				EL(acc,CTYPE_b,_i)=1; }}));
	} else {
		VARY_EACHLEFT(obj, pat, ({
			// TODO matchany(): buggy subscripting:
			if((pat->tag==0 || pat->tag==obj->tag) && _findbuf(pat, (buf_t)&_x) != -1) {
				PF("matchany found simple at %d\n", _i);
				EL(acc,CTYPE_b,_i) = 1;
			}
		}), typerr);
		IF_EXC(typerr>-1, Tt(type), "matchany could not match those types",obj,pat);
	}
	PF("matchany result\n"); DUMP(acc);
	return acc;
}
VP matcheasy(VP obj,VP pat) {
	IF_EXC(!SIMPLE(obj) && !LIST(obj),Tt(type),"matcheasy only works with numeric or string types in x",obj,pat);
	int j,n=obj->n,typerr=-1;VP item, acc;
	PF("matcheasy\n"); DUMP(obj); DUMP(pat);
	acc=xbsz(n); // TODO matcheasy() should be smarter about initial buffer size
	acc->n=n;

	if(CALLABLE(pat)) {
		PF("matcheasy callable\n");
		return each(obj, pat);
	}

	if(LIST(obj)) {
		FOR(0,n,({ 
			if((pat->tag == 0 || pat->tag==obj->tag) && _equal(ELl(obj,_i),pat)) 
				EL(acc,CTYPE_b,_i)=1; }));
	} else {
		VARY_EACHLEFT(obj, pat, ({
			if(_x == _y) EL(acc,CTYPE_b,_i) = 1;
		}), typerr);
		IF_EXC(typerr>-1, Tt(type), "matcheasy could not match those types",obj,pat);
	}
	PF("matcheasy result\n"); DUMP(acc);
	return acc;
}
VP matchexec(VP obj,VP pats) {
	int i,j,diff;VP rule,res,res2,sel;
	ASSERT(LIST(pats)&&pats->n%2==0,"pats should be a list of [pat1,fn1,pat2,fn2..]");
	PF("matchexec start\n");
	DUMP(obj);
	DUMP(pats);
	for(i=0;i<pats->n;i+=2) {
		PF("matchexec %d\n", i);
		rule=apply(pats,xi(i));
		if(IS_t(rule)) 
			res=matchtag(obj,rule);
		else
			res=matchany(obj,rule);
		PF("matchexec match, rule and res:\n");
		DUMP(rule);
		DUMP(res);
		// rules start with an unimportant first item: empty tag for tagmatch
		if(_any(res)) {
			VP indices = partgroups(condense(res));
			diff = 0;
			for (j=0; j<indices->n; j++) {
				VP idx = ELl(indices, j);
				PF("matchexec idx, len=%d, diff=%d\n", idx->n, diff); DUMP(idx);
				res2=apply(ELl(pats,i+1),apply(obj,plus(idx, xi(diff))));
				if(LIST(res2) && res2->n == 0) continue;
				PF("matchexec after apply, len=%d\n", res2->n);
				DUMP(res2);
				obj=splice(obj,plus(idx, xi(diff)),res2);
				diff += 1 - idx->n;
				PF("matchexec new obj, diff=%d", diff);
				DUMP(obj);
			}
		}
	}	
	PF("matchexec done");
	DUMP(obj);
	return obj;
}
VP matchtag(VP obj,VP pat) {
	IF_EXC(!SIMPLE(obj) && !LIST(obj),Tt(type),"matchtag only works with numeric or string types in x",obj,pat);
	int j,n=obj->n,typerr=-1;VP item, acc;
	PF("matchtag\n"); DUMP(obj); DUMP(pat);
	acc=xbsz(n); // TODO matcheasy() should be smarter about initial buffer size
	acc->n=n;
	if(LIST(obj)) {
		FOR(0,n,({ 
			if(AS_t(pat,0) == ELl(obj,_i)->tag) 
				EL(acc,CTYPE_b,_i)=1; }));
	} else {
		VARY_EACHLEFT(obj, pat, ({
			if(AS_t(pat,0) == obj->tag) EL(acc,CTYPE_b,_i) = 1;
		}), typerr);
		IF_EXC(typerr>-1, Tt(type), "matchtag could not match those types",obj,pat);
	}
	PF("matchtag result\n"); DUMP(acc);
	return acc;
}
VP mklexer(const char* chars, const char* label) {
	return xln(2,
		entags(xfroms(chars),"raw"),
		mkproj(2,&labelitems,xfroms(label),0)
	);
	/*
	VP res = xlsz(2);
	return xln(2,
		entags(xln(2,
			Tt(raw),
			entags(split(xfroms(chars),xl0()),"anyof")
		),"greedy"),
		mkproj(2,&labelitems,xfroms(label),0)
	);
	*/
}
VP mkstr(VP x) {
	return entags(flatten(x),"string");
	// return flatten(x);
}

// CONTEXTS:

VP rootctx() {
	VP res;
	res=xd0();
	// postfix/unary operators
	res=assign(res,xt(_tagnums("]")),x1(&identity));
	res=assign(res,Tt(condense),x1(&condense));
	res=assign(res,Tt(info),x1(&info));
	res=assign(res,Tt(last),x1(&last));
	res=assign(res,Tt(len),x1(&len));
	res=assign(res,Tt(min),x1(&min));
	res=assign(res,Tt(max),x1(&max));
	res=assign(res,Tt(parse),x1(&parse));
	res=assign(res,Tt(repr),x1(&repr));
	res=assign(res,Tt(rev),x1(&reverse));
	res=assign(res,Tt(sum),x1(&sum));
	res=assign(res,Tt(til),x1(&til));
	res=assign(res,Tt(ver),xi(0));
	// infix/binary operators
	res=assign(res,Tt(deal),x2(&deal));
	res=assign(res,xt(_tagnums(",")),x2(&join)); // gcc gets confused by Tt(,) - thinks its two empty args
	res=assign(res,Tt(:),x2(&dict)); // gcc gets confused by Tt(,) - thinks its two empty args
	res=assign(res,Tt(+),x2(&plus));
	res=assign(res,Tt(-),x2(&plus));
	res=assign(res,Tt(*),x2(&times));
	res=assign(res,Tt(/),x2(&times));
	res=assign(res,Tt(%),x2(&mod));
	res=assign(res,Tt(|),x2(&or));
	res=assign(res,Tt(&),x2(&and));
	res=assign(res,xt(_tagnums("<")),x2(&lesser));
	res=assign(res,xt(_tagnums(">")),x2(&greater));
	res=assign(res,xt(_tagnums("[")),x2(&list2));
	res=assign(res,Tt(~),x2(&matcheasy));
	res=assign(res,Tt(!),x2(&amend));
	res=assign(res,Tt(bracketj),x2(&bracketjoin));
	res=assign(res,Tt(consecj),x2(&consecutivejoin));
	res=assign(res,Tt(drop),x2(&drop));
	res=assign(res,Tt(in),x2(&matchany));
	res=assign(res,Tt(pick),x2(&pick));
	res=assign(res,Tt(rot),x2(&shift));
	res=assign(res,Tt(take),x2(&take));
	// 
	// {min x mod 2_til x}
	// 30 as 'n til drop 2%n
	// {til drop 2%x min} as 'isprime; isprime 30
	//
	res=assign(res,Tt(@),x2(&each));
	res=assign(res,Tt(over),x2(&over));
	PF("rootctx returning\n");
	DUMP(res);
	return res;
}
VP mkworkspace() {
	char name[8];
	VP root,res,locals;
	snprintf(name,sizeof(name),"wk%-6d", rand());
	res=xx0(); root=rootctx(); locals=xd0();
	assign(root,Tt(name),locals);
	assign(locals,Tt(wkspc),xfroms(name));
	res=append(res,root);
	res=append(res,locals);
	return res;
}
VP eval(VP code) {
	ASSERT(1, "eval nyi");
	return (VP)0;
}
VP list2vec(VP obj) {
	// Collapses lists that contain all the same kind of vector items into a
	// single vector [1,2,3i] = (1,2,3i) Will NOT collapse [1,(2,3),4] - use
	// flatten for this. (See note below for non-flat first items) The original
	// list will be returned when rejected for massaging.
	int i, t;
	VP acc,this;
	PF("list2vec\n"); DUMP(obj);
	if(!LIST(obj)) return obj;
	acc=ALLOC_LIKE(ELl(obj,0));
	if(obj->tag!=0) acc->tag=obj->tag;
	FOR(0,obj->n,({ this=ELl(obj,_i);
		// bomb out on non-scalar items or items of a different type than the first
		// note: we allow the first item to be nonscalar to handle the list2vec
		// [(0,1)] case - this is technically not a scalar first item, but clearly
		// it should return (0,1)
		if((_i > 0 && !SCALAR(this)) || this->t != acc->t){xfree(acc); return obj; } 
		else append(acc,this); }));
	PF("list2vec result\n"); DUMP(acc); DUMP(info(acc));
	return acc;
}
VP parseexpr(VP x) {
	PF("parseexpr\n");DUMP(x);
	if(LIST(x) && IS_c(ELl(x,0)) && AS_c(ELl(x,0),0)=='(')
		return drop_(drop_(x,-1),1);
	else
		return x;
}
VP parselambda(VP x) {
	int i,arity=1,typerr=-1,traw=Ti(raw); VP this;
	PF("parselambda\n");DUMP(x);
	x=list(x);
	for(i=0;i<x->n;i++) {
		this=ELl(x,i);
		if(IS_c(this) && this->tag==traw && AS_c(this,0)=="y") { 
			arity=2;break;
		}
	};
	if(LIST(x) && IS_c(ELl(x,0)) && AS_c(ELl(x,0),0)=='{')
		return entags(xln(2,drop_(drop_(x,-1),1),xi(arity)),"lambda");
	else return x;
}
VP parsestrlit(VP x) {
	int i,arity=1,typerr=-1,traw=Ti(raw); VP this;
	PF("parsestrlit\n");DUMP(x);
	// sleep(2);
	if(IS_c(x) && AS_c(x,0)=='"') 
		return drop_(drop_(x,-1),1);
	else {
		PF("parsestrlit not interested in\n");
		DUMP(x);
		return x;
	}
}
VP parsestr(const char* str) {
	VP ctx,lex,pats,acc,t1,t2;size_t l=strlen(str);int i;
	PF("parsestr '%s'\n",str);
	acc=xlsz(l);
	for(i=0;i<l;i++)
		append(acc,entags(xc(str[i]),"raw"));
	if(AS_c(ELl(acc,acc->n - 1),0)!='\n')
		append(acc,entags(xc('\n'),"raw"));
	// DUMP(acc);
	
	// consider using nest instead .. more direct
	/*
	acc=nest(acc,xln(4, xfroms("//"), xfroms("//"), xfroms(""), Tt(comment)));
	acc=nest(acc,xln(4, xfroms("//"), xfroms("\n"), xfroms(""), Tt(comment)));
	*/
	ctx=mkworkspace();
	pats=xln(3,
		mkproj(2,&nest,0,xln(4, xfroms("//"), xfroms("\n"), xfroms(""), Tt(comment))),
		mkproj(2,&nest,0,xln(4, xfroms("/*"), xfroms("*/"), xfroms(""), Tt(comment))),
		mkproj(2,&nest,0,xln(4, xfroms("\""), xfroms("\""), xfroms(""), Tt(string)))
	);
	ctx=append(ctx,pats);
	acc=exhaust(acc,ctx);
	/*
		acc=nest(acc,xln(4, xfroms("\""), xfroms("\""), xfroms(""), Tt(string)));
	*/
	// acc=deep(acc,x1(&list2vec));
	PF("parsestr after nest\n");
	xfree(pats);

	pats=xl0();
	/*
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
	lex = xln(2,
		entags(xln(2,
			Tt(raw),
			xfroms("\""),
			xc0(),
			xfroms("\"")
		),"greedy"),
		mkproj(2,&labelitems,xfroms("string"),0)
	);
	append(pats,ELl(lex,0));
	append(pats,ELl(lex,1));
	xfree(lex);
	*/
	lex=mklexer("0123456789","int");
	append(pats,ELl(lex,0));
	append(pats,ELl(lex,1));
	xfree(lex);
	lex=mklexer("'abcdefghijlmnopqrstuvwxyz.?","name");
	append(pats,ELl(lex,0));
	append(pats,ELl(lex,1));
	xfree(lex);
	//lex=mklexer(" \n\t\r","ws");
	lex=mklexer(" \n\t","ws");
	append(pats,ELl(lex,0));
	append(pats,ELl(lex,1));
	xfree(lex);
	lex=mklexer("\n","ws");
	append(pats,ELl(lex,0));
	append(pats,ELl(lex,1));
	xfree(lex);

	append(pats,Tt(int));
	append(pats,x1(&str2int));

	append(pats,Tt(name));
	append(pats,x1(&name2sym));

	// append(pats,Tt(expr));
	// append(pats,x1(&parseexpr));

	t1=matchexec(acc,pats);

	xfree(pats);
	xfree(acc);
	//xfree(ctx);
	PF("matchexec results\n");DUMP(t1);

	ctx=mkworkspace();
	pats=xln(2,
		mkproj(2,&nest,0,xln(4, xfroms("("), xfroms(")"), xfroms(""), Tt(expr))),
		mkproj(2,&nest,0,xln(4, xfroms("{"), xfroms("}"), xfroms(""), Tt(lambda)))
	);
	ctx=append(ctx,pats);
	t2=exhaust(t1,ctx);
	PF("*!*!*!*!*!*!*! matchexec form nesting\n"); DUMP(t2);

	pats=xl0();
	append(pats,Tt(string));
	append(pats,x1(&parsestrlit));
	append(pats,Tt(expr));
	append(pats,x1(&parseexpr));
	append(pats,Tt(lambda));
	append(pats,x1(&parselambda));
	t2=matchexec(t2,pats);
	//t2=exhaust(t2,mkproj(2,&matchexec,0,pats));
	return t2;
}

VP parse(VP x) {
	return parsestr(sfromx(x));
}

// THREADING

void thr_start() {
	// TODO threading on Windows
	#ifndef THREAD
	#else
	NTHR=0;
	#endif
	return;
}
void* thr_run0(void *fun(void*)) {
	#ifndef THREAD
	#else
	(*fun)(NULL); pthread_exit(NULL);
	#endif
	return NULL;
}
void thr_run(void *fun(void*)) {
	#ifndef THREAD
	#else
	pthread_attr_t a; pthread_attr_init(&a); pthread_attr_setdetachstate(&a, PTHREAD_CREATE_JOINABLE);
	// nthr=sysconf(_SC_NPROCESSORS_ONLN);if(nthr<2)nthr=2;
	WITHLOCK(thr,pthread_create(&THR[NTHR++], &a, &thr_run0, fun));
	#endif
	return;
}
void thr_wait() {
	#ifndef THREAD
	#else
	void* _; int i; for(i=0;i<NTHR;i++) pthread_join(THR[i],&_);
	return;
	#endif
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
	//printf("%lld\n", AS_o(c,0));
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
	return;
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
	VP a;
	printf("TEST_EVAL\n");
	ASSERT(
		_equal(
			parsestr("// "),
			entags(xfroms("// \n"),"comment")
		), "tec0");
	ASSERT(
		_equal(
			parsestr("/* */"),
			xln(2,entags(xfroms("/* */"),"comment"),xfroms("\n"))
		), "tec0b");
	ASSERT(
		_equal(
			parsestr("/*a*/ /*z*/"),
			xln(4,
				entags(xfroms("/*a*/"),"comment"),
				xfroms(" "),
				entags(xfroms("/*z*/"),"comment"),
				xfroms("\n"))
		), "tec1bbbb");
	ASSERT(
		_equal(
			parsestr("//x"),
			xl(entags(xfroms("//x\n"),"comment"))
		), "tec1");
	ASSERT(
		_equal(
			parsestr("/*x*/"),
			xln(2,entags(xfroms("/*x*/"),"comment"),xfroms("\n"))
		), "tec1b");
	ASSERT(
		_equal(
			parsestr("//xy"),
			xl(entags(xfroms("//xy\n"),"comment"))
		), "tec1c");
	ASSERT(
		_equal(
			parsestr("//x "),
			xl(entags(xfroms("//x \n"),"comment"))
		), "tec2");
	ASSERT(
		_equal(
			parsestr("// x"),
			xl(entags(xfroms("// x\n"),"comment"))
		), "tec2b");
	ASSERT(
		_equal(
			parsestr("// abc "),
			xl(entags(xfroms("// abc \n"),"comment"))
		), "tec3");
	ASSERT(
		_equal(
			parsestr("// a\n//b"),
			xln(2,
				entags(xfroms("// a\n"),"comment"),
				entags(xfroms("//b\n"),"comment"))
		), "tec4");
	ASSERT(
		_equal(
			parsestr("/* abc */"),
			xln(2,entags(xfroms("/* abc */"),"comment"),xfroms("\n"))
		), "tec5");
	ASSERT(
		_equal(
			parsestr("1"),
			xln(2,xi(1),xfroms("\n"))
		), "tei0");
	ASSERT(
		_equal(
			parsestr("1//blah"),
			xln(2,
				xi(1),
				entags(xfroms("//blah\n"),"comment"))
		), "teic0");
	ASSERT(
		_equal(
			parsestr("1//blah\n2"),
			xln(4,
				xi(1),
				entags(xfroms("//blah\n"),"comment"),
				xi(2),
				xfroms("\n")
			)
		), "teic1");
	//DUMP(parsestr("// test"));
	//parsestr("// test\nx:\"Hello!\"\ntil 1024");
}
void test_ctx() {
	VP ctx,tmp1,tmp2;
	
	printf("TEST_CTX\n");
	#include "test-ctx.h"	
}
void test_deal_speed() {
	int i;
	VP a,b,c;
	// xprofile_start();
	
	a=xi(1024 * 1024);b=xi(100);
	TIME(100, ({ c=deal(a,b); xfree(c); }));
}
void test_json() {
	VP mask, jsrc, res; char str[256]={0};
	strncpy(str,"[[\"abc\",5,[\"def\"],6,[7,[8,9]]]]",256);
	jsrc=split(xfroms(str),xc0());
	DUMP(jsrc);
	res=nest(jsrc,xln(2,xfroms("["),xfroms("]")));
	DUMP(res);
	DUMP(each(res, x1(&repr)));
	exit(1);
}
void test_nest() {
	VP a,b,c;
	printf("TEST_NEST\n");
	#include"test-nest.h"
	xfree(a);xfree(b);xfree(c);
}
VP evalstr(VP ctx,VP str) {
}
void evalfile(VP ctx,const char* fn) {
	#define EFBLK 65535
	int fd,r;char buf[EFBLK];VP acc=xcsz(1024),res;
	fd=open(fn,O_RDONLY);
	if(fd<0)return perror("evalfile open");
	do {
		r=read(fd,buf,EFBLK);
		if(r<0)perror("evalfile read");
		else appendbuf(acc,(buf_t)buf,r);
	} while (r==EFBLK);

	PFW({
	PF("evalfile executing\n"); DUMP(acc);
	append(ctx,parsestr(sfromx(acc)));
	res=apply(ctx,xl0()); // TODO define global constant XNULL=xl0(), XI1=xi(1), XI0=xi(0), etc..
	PF("evalfile done"); DUMP(res);
	});
	exit(1);
}
void tests() {
	int i;
	VP a,b,c;
	// xprofile_start();
	
	if (DEBUG) {
		// xprofile_start();
		printf("TESTS START\n");
		test_basics();
		test_match();
		test_nest();
		// test_json();
		test_ctx();
		test_eval();
		printf("TESTS PASSED\n");
		// test_proj_thr();
		// xprofile_end();
		if(MEM_W) {
			PF("alloced = %llu, freed = %llu\n", MEM_ALLOC_SZ, MEM_FREED_SZ);
		}
	}
}
int main(int argc, char* argv[]) {
	VP ctx=mkworkspace();
	// net();
	if(argc == 2) evalfile(ctx,argv[1]);
	else tests();
	repl();
	exit(1);
}
/*
	
	TODO decide operator for typeof
	TODO decide operator for tagof
	TODO decide operator for applytag
	TODO can we auto-parallelize some loops?

*/ 
