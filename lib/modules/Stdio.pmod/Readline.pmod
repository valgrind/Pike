// $Id: Readline.pmod,v 1.1 1999/03/13 01:12:37 marcus Exp $

class OutputController
{
  static private object outfd, term;
  static private int xpos = 0, columns = 0;

  void check_columns()
  {
    catch {
      int c = outfd->tcgetattr()->columns;
      if(c)
	columns = c;
    };
    if(!columns)
      columns = term->tgetnum("co") || 80;
  }

  int get_number_of_columns()
  {
    return columns;
  }

  static string escapify(string s)
  {
    for(int i=0; i<strlen(s); i++)
      if(s[i]<' ')
	s = s[..i-1]+sprintf("^%c", s[i]+'@')+s[i+1..];
      else if(s[i]==127)
	s = s[..i-1]+"^?"+s[i+1..];
      else if(s[i]>=128 && s[i]<160)
	s = s[..i-1]+sprintf("~%c", s[i]-128+'@')+s[i+1..];
    return s;
  }

  static int width(string s)
  {
    return strlen(s);
  }

  static int escapified_width(string s)
  {
    return width(escapify(s));
  }

  void low_write(string s)
  {
    int n = width(s);
    if(!n)
      return;
    while(xpos+n>=columns) {
      int l = columns-xpos;
      outfd->write(s[..l-1]);
      s = s[l..];
      n -= l;
      xpos = 0;
      if(!term->tgetflag("am"))
	outfd->write("\r\n");
    }
    if(xpos==0 && term->tgetflag("am"))
      outfd->write(" "+term->put("le"));
    if(n>0) {
      outfd->write(s);
      xpos += n;
    }
  }

  void write(string s)
  {
    low_write(escapify(s));
  }

  void low_move_downward(int n)
  {
    if(n<=0)
      return;
    outfd->write(term->put("DO", n) || (term->put("do")||"")*n);
  }

  void low_move_upward(int n)
  {
    if(n<=0)
      return;
    outfd->write(term->put("UP", n) || (term->put("up")||"")*n);
  }

  void low_move_forward(int n)
  {
    if(n<=0)
      return;
    if(xpos+n<columns) {
      outfd->write(term->put("RI", n) || (term->put("ri")||"")*n);
      xpos += n;
    } else {
      int l = (xpos+n)/columns;
      low_move_downward(l);
      n -= l*columns;
      if(n<0)
	low_move_backward(-n);
      else if(n>0)
	low_move_forward(n);
    }
  }

  void low_move_backward(int n)
  {
    if(n<=0)
      return;
    if(xpos-n>=0) {
      outfd->write(term->put("LE", n) || (term->put("le")||"")*n);
      xpos -= n;
    } else {
      int l = 1+(n-xpos-1)/columns;
      low_move_upward(l);
      n -= l*columns;
      if(n<0)
	low_move_forward(-n);
      else if(n>0)
	low_move_backward(n);
    }
  }

  void low_erase(int n)
  {
    string e = term->put("ec", n);
    if (e)
      outfd->write(e);
    else
    {
      low_write(" "*n);
      low_move_backward(n);
    }
  }

  void move_forward(string s)
  {
    low_move_forward(escapified_width(s));
  }

  void move_backward(string s)
  {
    low_move_backward(escapified_width(s));
  }

  void erase(string s)
  {
    low_erase(escapified_width(s));
  }

  void newline()
  {
    outfd->write("\r\n");
    xpos = 0;
  }

  void bol()
  {
    outfd->write("\r");
    xpos = 0;
  }

  void clear(int|void partial)
  {
    string s;
    if(!partial && (s = term->put("cl"))) {
      outfd->write(s);
      xpos = 0;
      return;
    }
    if(!partial) {
      outfd->write(term->put("ho")||term->put("cm", 0, 0)||"\f");
      xpos = 0;
    }
    outfd->write(term->put("cd")||"");
  }

  void create(object|void _outfd, object|string|void _term)
  {
    outfd = _outfd || Stdio.File("stdout");
    term = objectp(_term)? _term : .Terminfo.getTerm(_term);
    check_columns();
  }

}

class InputController
{
  static private object infd, term;
  static private int enabled = -1;
  static private function(:int) close_callback = 0;
  static private string prefix="";
  static private mapping(int:function|mapping(string:function)) bindings=([]);
  static private function grab_binding = 0;
  static private mapping oldattrs = 0;

  void destroy()
  {
    catch{ infd->set_blocking(); };
    catch{ if(oldattrs) infd->tcsetattr(oldattrs); };
    catch{ infd->tcsetattr((["ECHO":1,"ICANON":1])); };
  }

  static private string process_input(string s)
  {
    int i;

    for (i=0; i<sizeof(s); i++)
    {
      if (!enabled)
	return s[i..];
      function|mapping(string:function) b = grab_binding || bindings[s[i]];
      grab_binding = 0;
      if (!b)
	/* do nothing */;
      else if(mappingp(b)) {
	int ml = 0, l = sizeof(s)-i;
	string m;
	foreach (indices(b), string k)
	{
	  if (sizeof(k)>l && k[..l-1]==s[i..])
	    /* Valid prefix, need more data */
	    return s[i..];
	  else if (sizeof(k) > ml && s[i..i+sizeof(k)-1] == k)
	  {
	    /* New longest match found */
	    ml = sizeof(m = k);
	  }
	}
	if (ml)
	{
	  i += ml-1;
	  b[m](m);
	}
      } else
	b(s[i..i]);
    }
    return "";
  }

  static private void read_cb(mixed _, string s)
  {
    if (!s || s=="")
      return;
    if (sizeof(prefix))
    {
      s = prefix+s;
      prefix = "";
    }
    prefix = process_input(s);
  }

  static private void close_cb()
  {
    if (close_callback && close_callback())
      return;
    destruct(this_object());
  }

  static private int set_enabled(int e)
  {
    if (e != enabled)
    {
      enabled = e;
      if (enabled)
      {
	string oldprefix = prefix;
	prefix = "";
	prefix = process_input(oldprefix);
	infd->set_nonblocking(read_cb, 0, close_cb);
      }
      else
	infd->set_blocking();
      return !enabled;
    }
    else
      return enabled;
  }

  int isenabled()
  {
    return enabled;
  }

  int enable(int ... e)
  {
    if (sizeof(e))
      return set_enabled(!!e[0]);
    else
      return set_enabled(1);
  }

  int disable()
  {
    return set_enabled(0);
  }

  void run_blocking()
  {
    disable();
    string data = prefix;
    prefix = "";
    enabled = 1;
    for (;;)
    {
      prefix = process_input(data);
      if (!enabled)
	return;
      data = prefix+infd->read(1024, 1);
      prefix = "";
    }
  }

  void set_close_callback(function (:int) ccb)
  {
    close_callback = ccb;
  }

  void nullbindings()
  {
    bindings = ([]);
  }

  void grabnextkey(function g)
  {
    if(functionp(g))
      grab_binding = g;
  }

  function bindstr(string str, function f)
  {
    function oldf = 0;
    if (mappingp(f))
      f = 0; // Paranoia
    switch (sizeof(str||""))
    {
    case 0:
      break;
    case 1:
      if (mappingp(bindings[str[0]]))
      {
	oldf = bindings[str[0]][str];
	if (f)
	  bindings[str[0]][str] = f;
	else
	  m_delete(bindings[str[0]], str);
      } else {
	oldf = bindings[str[0]];
	if (f)
	  bindings[str[0]] = f;
	else
	  m_delete(bindings, str[0]);
      }
      break;
    default:
      if (mappingp(bindings[str[0]]))
	oldf = bindings[str[0]][str];
      else
	bindings[str[0]] =
	  bindings[str[0]]? ([str[0..0]:bindings[str[0]]]) : ([]);
      if (f)
	bindings[str[0]][str] = f;
      else {
	m_delete(bindings[str[0]], str);
	if (!sizeof(bindings[str[0]]))
	  m_delete(bindings, str[0]);
	else if(sizeof(bindings[str[0]])==1 && bindings[str[0]][str[0..0]])
	  bindings[str[0]] = bindings[str[0]][str[0..0]];
      }
      break;
    }
    return oldf;
  }

  function unbindstr(string str)
  {
    return bindstr(str, 0);
  }

  function getbindingstr(string str)
  {
    switch (sizeof(str||""))
    {
    case 0:
      return 0;
    case 1:
      return mappingp(bindings[str[0]])?
	bindings[str[0]][str] : bindings[str[0]];
    default:
      return mappingp(bindings[str[0]]) && bindings[str[0]][str];
    }
  }

  function bindtc(string cap, function f)
  {
    return bindstr(term->tgetstr(cap), f);
  }

  function unbindtc(string cap)
  {
    return unbindstr(term->tgetstr(cap));
  }

  function getbindingtc(string cap)
  {
    return getbindingstr(term->tgetstr(cap));
  }

  string parsekey(string k)
  {
    if (k[..1]=="\\!")
      k = term->tgetstr(k[2..]);
    else for(int i=0; i<sizeof(k); i++)
      switch(k[i])
      {
      case '\\':
	if(i<sizeof(k)-1) 
	  switch(k[i+1]) {
	  case '0':
	  case '1':
	  case '2':
	  case '3':
	  case '4':
	  case '5':
	  case '6':
	  case '7':
	    int n, l;
	    if (2<sscanf(k[i+1..], "%o%n", n, l))
	    {
	      n = k[i+1]-'0';
	      l = 1;
	    }
	    k = k[..i-1]+sprintf("%c", n)+k[i+1+l..];
	    break;
	  case 'e':
	  case 'E':
	    k = k[..i-1]+"\033"+k[i+2..];
	    break;
	  case 'n':
	    k = k[..i-1]+"\n"+k[i+2..];
	    break;
	  case 'r':
	    k = k[..i-1]+"\r"+k[i+2..];
	    break;
	  case 't':
	    k = k[..i-1]+"\t"+k[i+2..];
	    break;
	  case 'b':
	    k = k[..i-1]+"\b"+k[i+2..];
	    break;
	  case 'f':
	    k = k[..i-1]+"\f"+k[i+2..];
	    break;
	  default:
	    k = k[..i-1]+k[i+1..];
	    break;
	  }
	break;
      case '^':
	if(i<sizeof(k)-1 && k[i+1]>='?' && k[i+1]<='_') 
	  k = k[..i-1]+(k[i+1]=='?'? "\177":sprintf("%c",k[i+1]-'@'))+k[i+2..];
	break;
      }
    return k;
  }

  function bind(string k, function f)
  {
    return bindstr(parsekey(k), f);
  }

  function unbind(string k)
  {
    return unbindstr(parsekey(k));
  }

  function getbinding(string k, string cap)
  {
    return getbindingstr(parsekey(k));
  }

  mapping(string:function) getbindings()
  {
    mapping(int:function) bb = bindings-Array.filter(bindings, mappingp);
    return `|(mkmapping(Array.map(indices(bb), lambda(int n) {
						 return sprintf("%c", n);
					       }), values(bb)),
	      @Array.filter(values(bindings), mappingp));
  }

  void create(object|void _infd, object|string|void _term)
  {
    infd = _infd || Stdio.File("stdin");
    term = objectp(_term)? _term : .Terminfo.getTerm(_term);
    disable();
    catch { oldattrs = infd->tcgetattr(); };
    catch { infd->tcsetattr((["ECHO":0,"ICANON":0,"VMIN":1,"VTIME":0,
			      "VLNEXT":0])); };
  }

}

class DefaultEditKeys
{

  static object _readline;

  void self_insert_command(string str)
  {
    _readline->insert(str, _readline->getcursorpos());
  }

  void quoted_insert()
  {
    _readline->get_input_controller()->grabnextkey(self_insert_command);
  }

  void newline()
  {
    _readline->newline();
  }

  void up_history()
  {
    _readline->delta_history(-1);
  }

  void down_history()
  {
    _readline->delta_history(1);
  }

  void backward_delete_char()
  {
    int p = _readline->getcursorpos();
    _readline->delete(p-1,p);
  }

  void delete_char_or_eof()
  {
    int p = _readline->getcursorpos();
    if (p<strlen(_readline->gettext()))
      _readline->delete(p,p+1);
    else if(!strlen(_readline->gettext()))
      _readline->eof();
  }

  void forward_char()
  {
    _readline->setcursorpos(_readline->getcursorpos()+1);
  }

  void backward_char()
  {
    _readline->setcursorpos(_readline->getcursorpos()-1);
  }

  void beginning_of_line()
  {
    _readline->setcursorpos(0);
  }

  void end_of_line()
  {
    _readline->setcursorpos(strlen(_readline->gettext()));
  }

  void transpose_chars()
  {
    int p = _readline->getcursorpos();
    if (p<0 || p>=strlen(_readline->gettext()))
      return;
    string c = _readline->gettext()[p-1..p];
    _readline->delete(p-1, p+1);
    _readline->insert(reverse(c), p-1);
  }

  void kill_line()
  {
    _readline->delete(_readline->getcursorpos(), strlen(_readline->gettext()));
  }

  void kill_whole_line()
  {
    _readline->delete(0, strlen(_readline->gettext()));
  }

  void redisplay()
  {
    _readline->redisplay(0);
  }

  void clear_screen()
  {
    _readline->redisplay(1);
  }

  static array(array(string|function)) default_bindings = ({
    ({ "^[[A", up_history }),
    ({ "^[[B", down_history }),
    ({ "^[[C", forward_char }),
    ({ "^[[D", backward_char }),
    ({ "^A", beginning_of_line }),
    ({ "^B", backward_char }),
    ({ "^D", delete_char_or_eof }),
    ({ "^E", end_of_line }),
    ({ "^F", forward_char }),
    ({ "^H", backward_delete_char }),
    ({ "^J", newline }),
    ({ "^K", kill_line }),
    ({ "^L", clear_screen }),
    ({ "^M", newline }),
    ({ "^N", down_history }),
    ({ "^P", up_history }),
    ({ "^R", redisplay }),
    ({ "^T", transpose_chars }),
    ({ "^U", kill_whole_line }),
    ({ "^V", quoted_insert }),
    ({ "^?", backward_delete_char }),
    ({ "\\!ku", up_history }),
    ({ "\\!kd", down_history }),
    ({ "\\!kr", forward_char }),
    ({ "\\!kl", backward_char })
  });

  static void set_default_bindings()
  {
    object ic = _readline->get_input_controller();
    ic->nullbindings();
    for(int i=' '; i<'\177'; i++)
      ic->bindstr(sprintf("%c", i), self_insert_command);
    for(int i='\240'; i<='\377'; i++)
      ic->bindstr(sprintf("%c", i), self_insert_command);
    foreach(default_bindings, array(string|function) b)
      ic->bind(@b);
  }

  void create(object readline)
  {
    _readline = readline;
    set_default_bindings();
  }

}

class History
{
  static private array(string) historylist;
  static private int minhistory, maxhistory, historynum;

  int get_history_num()
  {
    return historynum;
  }

  string history(int n, string text)
  {
    if (n<minhistory)
      n = minhistory;
    else if (n-minhistory>=sizeof(historylist))
      n = sizeof(historylist)+minhistory-1;
    historylist[historynum-minhistory]=text;
    return historylist[(historynum=n)-minhistory];
  }

  void initline()
  {
    if (sizeof(historylist)==0 || historylist[-1]!="") {
      historylist += ({ "" });
      if (maxhistory && sizeof(historylist)>maxhistory)
      {
	int n = sizeof(historylist)-maxhistory;
	historylist = historylist[n..];
	minhistory += n;
      }
    }
    historynum = sizeof(historylist)-1+minhistory;
  }

  void finishline(string text)
  {
    historylist[historynum-minhistory]=text;
  }

  void set_max_history(int maxhist)
  {
    maxhistory = maxhist;
  }

  void create(int maxhist)
  {
    historylist = ({ "" });
    minhistory = historynum = 0;
    maxhistory = maxhist;
  }

}

class Readline
{
  static private object(OutputController) output_controller;
  static private object(InputController) input_controller;
  static private string prompt="";
  static private string text="", readtext;
  static private function(string:void) newline_func;
  static private int cursorpos = 0;
  static private object(History) historyobj = 0;

  object(OutputController) get_output_controller()
  {
    return output_controller;
  }

  object(InputController) get_input_controller()
  {
    return input_controller;
  }

  string get_prompt()
  {
    return prompt;
  }

  string set_prompt(string newp)
  {
    string oldp = prompt;
    prompt = newp;
    return oldp;
  }

  string gettext()
  {
    return text;
  }

  int getcursorpos()
  {
    return cursorpos;
  }

  int setcursorpos(int p)
  {
    if (p<0)
      p = 0;
    if (p>strlen(text))
      p = strlen(text);
    if (p<cursorpos)
    {
      output_controller->move_backward(text[p..cursorpos-1]);
      cursorpos = p;
    }
    else if (p>cursorpos)
    {
      output_controller->move_forward(text[cursorpos..p-1]);
      cursorpos = p;
    }
    return cursorpos;
  }

  void insert(string s, int p)
  {
    if (p<0)
      p = 0;
    if (p>strlen(text))
      p = strlen(text);
    setcursorpos(p);
    output_controller->write(s);
    cursorpos += strlen(s);
    string rest = text[p..];
    if (strlen(rest))
    {
      output_controller->write(rest);
      output_controller->move_backward(rest);
    }
    text = text[..p-1]+s+rest;
  }

  void delete(int p1, int p2)
  {
    if (p1<0)
      p1 = 0;
    if (p2>strlen(text))
      p2 = strlen(text);
    setcursorpos(p1);
    if (p1>=p2)
      return;
    output_controller->write(text[p2..]);
    output_controller->erase(text[p1..p2-1]);
    text = text[..p1-1]+text[p2..];
    cursorpos = strlen(text);
    setcursorpos(p1);
  }

  void history(int n)
  {
    if(historyobj) {
      string h = historyobj->history(n, text);
      delete(0, sizeof(text));
      insert(h, 0);
    }
  }

  void delta_history(int d)
  {
    if(historyobj)
      history(historyobj->get_history_num()+d);
  }

  void redisplay(int clear, int|void nobackup)
  {
    int p = cursorpos;
    if(clear)
      output_controller->clear();
    else if(!nobackup) {
      setcursorpos(0);
      output_controller->bol();
      output_controller->clear(1);
    }
    output_controller->check_columns();
    if(newline_func == read_newline)
      output_controller->write(prompt);
    output_controller->write(text);
    cursorpos = sizeof(text);
    setcursorpos(p);
  }

  static private void initline()
  {
    text = "";
    cursorpos = 0;
    if (historyobj)
      historyobj->initline();
  }

  string newline()
  {
    setcursorpos(sizeof(text));
    output_controller->newline();
    string data = text;
    if (historyobj)
      historyobj->finishline(text);
    initline();
    if(newline_func)
      newline_func(data);
  }

  void eof()
  {
    if (historyobj)
      historyobj->finishline(text);
    initline();
    if(newline_func)
      newline_func(0);    
  }

  static private void read_newline(string s)
  {
    input_controller->disable();
    readtext = s;
  }

  void set_nonblocking(function f)
  {
    if (newline_func = f)
      input_controller->enable();
    else
      input_controller->disable();
  }

  void set_blocking()
  {
    set_nonblocking(0);
  }

  string read()
  {
    if(newline_func == read_newline)
      return 0;
    function oldnl = newline_func;
    output_controller->write(prompt);
    initline();
    newline_func = read_newline;
    readtext = "";
    input_controller->run_blocking();
    set_nonblocking(oldnl);
    return readtext;
  }

  void enable_history(object(History)|int hist)
  {
    if (objectp(hist))
      historyobj = hist;
    else if(!hist)
      historyobj = 0;
    else if(historyobj)
      historyobj->set_max_history(hist);
    else
      historyobj = History(hist);
  }

  void destroy()
  {
    destruct(input_controller);
    destruct(output_controller);
  }

  void create(object|void infd, object|string|void interm,
	      object|void outfd, object|string|void outterm)
  {
    output_controller = OutputController(outfd || infd, outterm || interm);
    input_controller = InputController(infd, interm);
    DefaultEditKeys(this_object());
  }
}



/* Emulation of old readline() function.  Don't use in new code. */

static private object(History) readline_history = History(512);

string readline(string prompt, function|void complete_callback)
{
  object rl = Readline();
  rl->enable_history(readline_history);
  rl->set_prompt(prompt);
  if(complete_callback)
    rl->get_input_controller()->
      bind("^I", lambda() {
		   array(string) compl = ({ });
		   string c, buf = rl->gettext();
		   int st = 0, point = rl->getcursorpos();
		   int wordstart = search(replace(reverse(buf),
						  ({"\t","\r","\n"}),
						  ({" "," "," "})),
					  " ", sizeof(buf)-point);
		   string word = buf[(wordstart>=0 && sizeof(buf)-wordstart)..
				    point-1];
		   while((c = complete_callback(word, st++, buf, point)))
		     compl += ({ c });
		   switch(sizeof(compl)) {
		   case 0:
		     break;
		   case 1:
		     rl->delete(point-sizeof(word), point);
		     rl->insert(compl[0], point-sizeof(word));
		     rl->setcursorpos(point-sizeof(word)+sizeof(compl[0]));
		     break;
		   default:
		     rl->setcursorpos(strlen(rl->gettext()));
		     rl->get_output_controller()->newline();
		     foreach(sprintf("%-"+rl->get_output_controller()->
				     get_number_of_columns()+"#s", compl*"\n")
			     /"\n", string l) {
		       rl->get_output_controller()->write(l);
		       rl->get_output_controller()->newline();
		     }
		     rl->redisplay(0, 1);
		     rl->setcursorpos(point);
		   }
		 });
  string res = rl->read();
  destruct(rl);
  return res;
}
