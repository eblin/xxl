
	ctx=mkworkspace();
	tmp1=xl(mkproj(2,&plus,xi(2),0));
	append(ctx,tmp1);
	tmp2=apply(ctx,xi(3));
	DUMP(tmp2);
	ASSERT(_equal(tmp2,xi(5)),"test ctx 0");
	xfree(ctx); xfree(tmp1); xfree(tmp2);

	PF("aa\n");
	ctx=mkworkspace();
	tmp1=xl(mkproj(2,&plus,0,xi(3)));
	append(ctx,tmp1);
	tmp2=apply(ctx,xi(3));
	DUMP(tmp2);
	ASSERT(_equal(tmp2,xi(6)),"test ctx 1");
	xfree(ctx); xfree(tmp1); xfree(tmp2);

	ctx=mkworkspace();
	tmp1=xln(2,mkproj(2,&plus,0,xi(2)),mkproj(2,&plus,0,xi(5)));
	append(ctx,tmp1);
	tmp2=apply(ctx,xi(3));
	DUMP(tmp2);
	ASSERT(_equal(tmp2,xi(10)),"test ctx 2");
	xfree(ctx); xfree(tmp1); xfree(tmp2);

	ctx=mkworkspace();
	tmp1=xln(2,xi(2),mkproj(2,&plus,xi(5),0));
	append(ctx,tmp1);
	tmp2=apply(ctx,xi(3));
	DUMP(tmp2);
	ASSERT(_equal(tmp2,xi(7)),"test ctx 3");
	xfree(ctx); xfree(tmp1); xfree(tmp2);

	PF("a\n");
	ctx=mkworkspace(); // simple unary function application
	tmp1=xl(mkproj(1,&til,0,0));
	append(ctx,tmp1);
	tmp2=apply(ctx,xi(4));
	DUMP(tmp2);
	ASSERT(_equal(tmp2,xin(4,0,1,2,3)),"test ctx 4");
	xfree(ctx); xfree(tmp1); xfree(tmp2);

	ctx=mkworkspace(); // try applying an index to a function result
	tmp1=xln(2,mkproj(1,&til,0,0),xi(2));
	append(ctx,tmp1);
	tmp2=apply(ctx,xi(5));
	DUMP(tmp2);
	ASSERT(_equal(tmp2,xi(2)),"test ctx 5");
	xfree(ctx); xfree(tmp1); xfree(tmp2);

	ctx=mkworkspace(); // subexpression
	tmp1=xln(2,xl(mkproj(2,&plus,xi(2),0)),mkproj(1,&til,0,0));
	append(ctx,tmp1);
	tmp2=apply(ctx,xi(4));
	DUMP(tmp2);
	ASSERT(_equal(tmp2,xin(6,0,1,2,3,4,5)),"test ctx 6");
	xfree(ctx); xfree(tmp1); xfree(tmp2);

	ctx=mkworkspace();
	append(ctx,parsestr("1+1"));
	tmp1=apply(ctx,xi(0));
	DUMP(tmp1);
	ASSERT(_equal(tmp1,xi(2)),"test parsestr 0");
	xfree(ctx);xfree(tmp1);

	ctx=mkworkspace();
	append(ctx,parsestr("+1"));
	tmp1=apply(ctx,xi(1));
	DUMP(tmp1);
	ASSERT(_equal(tmp1,xi(2)),"test parsestr 1");
	xfree(ctx);xfree(tmp1);

	ctx=mkworkspace();
	append(ctx,parsestr("1+x"));
	tmp1=apply(ctx,xi(1));
	DUMP(tmp1);
	ASSERT(_equal(tmp1,xi(2)),"test parsestr 2");
	xfree(ctx);xfree(tmp1);

	ctx=mkworkspace();
	append(ctx,parsestr("1+x*2"));
	tmp1=apply(ctx,xi(1));
	DUMP(tmp1);
	ASSERT(_equal(tmp1,xi(4)),"test parsestr 3");
	xfree(ctx);xfree(tmp1);

	ctx=mkworkspace();
	append(ctx,parsestr("1+x*2+4"));
	tmp1=apply(ctx,xi(1));
	DUMP(tmp1);
	ASSERT(_equal(tmp1,xi(8)),"test parsestr 4");
	xfree(ctx);xfree(tmp1);

	ctx=mkworkspace();
	append(ctx,parsestr("1,2"));
	tmp1=apply(ctx,xi(1));
	DUMP(tmp1);
	ASSERT(_equal(tmp1,xin(2,1,2)),"test parsestr 5");
	xfree(ctx);xfree(tmp1);

	ctx=mkworkspace();
	append(ctx,parsestr("1,2+x*2+4"));
	tmp1=apply(ctx,xi(1));
	DUMP(tmp1);
	ASSERT(_equal(tmp1,xin(2,8,10)),"test parsestr 6");
	xfree(ctx);xfree(tmp1);

	ctx=mkworkspace();
	append(ctx,parsestr("2,4 over +"));
	tmp1=apply(ctx,xi(1));
	DUMP(tmp1);
	ASSERT(_equal(tmp1,xi(6)),"test parsestr 6");
	xfree(ctx);xfree(tmp1);

	ctx=mkworkspace();
	append(ctx,parsestr("(2,4) over +"));
	tmp1=apply(ctx,xi(1));
	DUMP(tmp1);
	ASSERT(_equal(tmp1,xi(6)),"test parsestr 7");
	xfree(ctx);xfree(tmp1);

	ctx=mkworkspace();
	append(ctx,parsestr("1+(2,3)"));
	tmp1=apply(ctx,xi(1));
	DUMP(tmp1);
	ASSERT(_equal(tmp1,xin(2,3,4)),"test parsestr 8");
	xfree(ctx);xfree(tmp1);

	ctx=mkworkspace();
	append(ctx,parsestr("(1+2,3)+(2,3)"));
	tmp1=apply(ctx,xi(1));
	DUMP(tmp1);
	ASSERT(_equal(tmp1,xin(2,5,6)),"test parsestr 9");
	xfree(ctx);xfree(tmp1);

	ctx=mkworkspace();
	append(ctx,parsestr("(1+(2*3))"));
	tmp1=apply(ctx,xi(0));
	DUMP(tmp1);
	ASSERT(_equal(tmp1,xi(7)),"test parsestr 10");
	xfree(ctx);xfree(tmp1);

	ctx=mkworkspace();
	append(ctx,parsestr("/* test */2"));
	tmp1=apply(ctx,xi(0));
	DUMP(tmp1);
	ASSERT(_equal(tmp1,xi(2)),"test parsestr 11");
	xfree(ctx);xfree(tmp1);

	ctx=mkworkspace();
	append(ctx,parsestr("*100 //test"));
	tmp1=apply(ctx,xi(2));
	DUMP(tmp1);
	ASSERT(_equal(tmp1,xi(200)),"test parsestr 12");
	xfree(ctx);xfree(tmp1);

	ctx=mkworkspace();
	append(ctx,parsestr("1000 /* test */ * /* x */ (100*100) //test"));
	tmp1=apply(ctx,xi(2));
	DUMP(tmp1);
	ASSERT(_equal(tmp1,xi(10000000)),"test parsestr 13");
	xfree(ctx);xfree(tmp1);

	ctx=mkworkspace();
	append(ctx,parsestr("{x*3}"));
	tmp1=apply(ctx,xi(2));
	DUMP(tmp1);
	ASSERT(_equal(tmp1,xi(6)),"test parsestr 14");
	xfree(ctx);xfree(tmp1);

	ctx=mkworkspace();
	append(ctx,parsestr("{3*x}"));
	tmp1=apply(ctx,xi(2));
	DUMP(tmp1);
	ASSERT(_equal(tmp1,xi(6)),"test parsestr 15");
	xfree(ctx);xfree(tmp1);

	ctx=mkworkspace();
	append(ctx,parsestr("{*3}"));
	tmp1=apply(ctx,xi(2));
	DUMP(tmp1);
	ASSERT(_equal(tmp1,xi(6)),"test parsestr 16");
	xfree(ctx);xfree(tmp1);

	ctx=mkworkspace();
	append(ctx,parsestr("5 {x*y} x"));
	tmp1=apply(ctx,xi(2));
	DUMP(tmp1);
	ASSERT(_equal(tmp1,xi(10)),"test parsestr 16b");
	xfree(ctx);xfree(tmp1);

	ctx=mkworkspace();
	append(ctx,parsestr("5 {x*z} as 'f;3 f")); 
	tmp1=apply(ctx,xi(2));
	DUMP(tmp1);
	ASSERT(_equal(tmp1,xi(15)),"test parsestr 16c");
	xfree(ctx);xfree(tmp1);

	ctx=mkworkspace();
	append(ctx,parsestr("5 {x*z} as 'f;f 3")); // note - technically invalid but still works! nice
	tmp1=apply(ctx,xi(2));
	DUMP(tmp1);
	ASSERT(_equal(tmp1,xi(15)),"test parsestr 16d");
	xfree(ctx);xfree(tmp1);

	ctx=mkworkspace();
	append(ctx,parsestr(";* as 'f;4 f 6"));  // leading ; disables auto-left-projection of invisible x
	tmp1=apply(ctx,xi(2));
	DUMP(tmp1);
	ASSERT(_equal(tmp1,xi(24)),"test parsestr 16e");
	xfree(ctx);xfree(tmp1);

	ctx=mkworkspace();
	append(ctx,parsestr("{til}"));
	tmp1=apply(ctx,xi(3));
	DUMP(tmp1);
	ASSERT(_equal(tmp1,xin(3,0,1,2)),"test parsestr 17");
	xfree(ctx);xfree(tmp1);

	ctx=mkworkspace();
	append(ctx,parsestr("til+2@{til}"));
	tmp1=apply(ctx,xi(3));
	DUMP(tmp1);
	ASSERT(_equal(tmp1,xln(3,xin(2,0,1),xin(3,0,1,2),xin(4,0,1,2,3))),"test parsestr 18");
	xfree(ctx);xfree(tmp1);

	ctx=mkworkspace();
	tmp1=xd0();
	assign(tmp1,Tt(y),xi(7));
	append(ctx,tmp1);
	append(ctx,parsestr("+y*3"));
	tmp1=apply(ctx,xi(2));
	DUMP(tmp1);
	ASSERT(_equal(tmp1,xi(27)),"test parsestr 19");
	xfree(ctx);xfree(tmp1);

	ctx=mkworkspace();
	tmp1=xd0();
	assign(tmp1,Tt(y),xi(7));
	append(ctx,tmp1);
	append(ctx,parsestr("x as 'n * 2 as 'doublen"));
	tmp1=apply(ctx,xi(2));
	DUMP(tmp1);
	ASSERT(_equal(tmp1,xi(4)),"test parsestr 20");
	xfree(ctx);xfree(tmp1);

	PFW({
	ctx=mkworkspace();
	append(ctx,parsestr("\"z\""));
	tmp1=apply(ctx,xi(0));
	DUMP(tmp1);
	ASSERT(_equal(tmp1,entags(xc('z'),"string")),"test parsestr 21");
	xfree(ctx);xfree(tmp1);
	});

	ctx=mkworkspace();
	append(ctx,parsestr("\"a\" as 's;s+1"));
	tmp1=apply(ctx,xi(0));
	DUMP(tmp1);
	ASSERT(_equal(tmp1,entags(xc('b'),"string")),"test parsestr 22");
	xfree(ctx);xfree(tmp1);

	ctx=mkworkspace();
	append(ctx,parsestr("[1,2,3]"));
	tmp1=apply(ctx,xi(0));
	DUMP(tmp1);
	ASSERT(_equal(tmp1,xln(3,xi(1),xi(2),xi(3))),"test parsestr list literal 23");
	xfree(ctx);xfree(tmp1);

	ctx=mkworkspace();
	append(ctx,parsestr("['a:1]"));
	tmp1=apply(ctx,xi(0));
	DUMP(tmp1);
	ASSERT(_equal(apply(tmp1,Tt(a)),xi(1)),"test parsestr dict literal 24");
	xfree(ctx);xfree(tmp1);

	ctx=mkworkspace();
	append(ctx,parsestr("['a:1,'b:2]"));
	tmp1=apply(ctx,xi(0));
	DUMP(tmp1);
	ASSERT(_equal(repr(tmp1),xfroms("'dict['a:1i, 'b:2i]\n")),"test parsestr dict literal 25");
	xfree(ctx);xfree(tmp1);

	ctx=mkworkspace();
	ctx=append(ctx,parsestr("[\"aaa\"]"));
	tmp2=apply(ctx,xi(0));
	ctx=append(ctx,parsestr("(\"aaa\")"));
	tmp1=apply(ctx,xi(0));
	DUMP(tmp2);
	DUMP(tmp1);
	// ASSERT(_equal(tmp1,tmp2),"test parsestr string equivalence 0");
	xfree(tmp1);xfree(tmp2);

	ctx=mkworkspace();
	ctx=append(ctx,parsestr("[1,\"aaa\"]"));
	tmp2=apply(ctx,xi(0));
	ctx=append(ctx,parsestr("(1,\"aaa\")"));
	tmp1=apply(ctx,xi(0));
	DUMP(tmp2);
	DUMP(tmp1);
	// ASSERT(_equal(tmp1,tmp2),"test parsestr string equivalence 1");
	xfree(tmp1);xfree(tmp2);

	ctx=mkworkspace();
	append(ctx,parsestr("['a:(1,2),'b:\"barf\"]"));
	tmp1=apply(ctx,xi(0));
	DUMP(tmp1);
	ASSERT(_equal(repr(tmp1),xfroms("'dict['a:(1,2i), 'b:'string(\"barf\")]\n")),"test parsestr dict literal 25");
	xfree(ctx);xfree(tmp1);

	ctx=mkworkspace();
	append(ctx,parsestr("('a,'b):((1,2),\"barf\")"));
	tmp1=apply(ctx,xi(0));
	DUMP(tmp1);
	ASSERT(_equal(repr(tmp1),xfroms("'dict['a:(1,2i), 'b:'string(\"\\\"barf\\\"\")]\n")),"test parsestr dict literal 25");
	// ^ note this is an example of a bug that still exists - see quotes inside string
	xfree(ctx);xfree(tmp1);

	/* currently fails: 
	 *
	ctx=mkworkspace();
	append(ctx,parsestr("{x*2} as 'double;7 double"));
	tmp1=apply(ctx,xi(0));
	DUMP(tmp1);
	ASSERT(_equal(tmp1,xi(14)),"test parsestr 23");
	xfree(ctx);xfree(tmp1);
	*/


