
class support {

  inherit "MIME";

  string generate_boundary( )
  {
    return "'ThIs-RaNdOm-StRiNg-/=_."+random(1000000000)+":";
  }
  
  string decode( string data, string encoding )
  {
    switch(lower_case(encoding||"binary")) {
    case "base64":
      return decode_base64( data );
    case "quoted-printable":
      return decode_qp( data );
    case "x-uue":
      return decode_uue( data );
    case "7bit": case "8bit": case "binary":
      return data;
    default:
      throw(({ "unknown transfer encoding "+encoding+"\n",
		 backtrace() }));
    }
  }
  
  string encode( string data, string encoding, void|string filename )
  {
    switch(lower_case(encoding||"binary")) {
    case "base64":
      return encode_base64( data );
    case "quoted-printable":
      return encode_qp( data );
    case "x-uue":
      return encode_uue( data, filename );
    case "7bit": case "8bit": case "binary":
      return data;
    default:
      throw(({ "unknown transfer encoding "+encoding+"\n",
		 backtrace() }));
    }
  }



  /* rfc1522 */

  array(string) decode_word( string word )
  {
    string charset, encoding, encoded_text;
    if(sscanf(word, "=?%[^][ \t()<>@,;:\"\\/?.=]?%[^][ \t()<>@,;:\"\\/?.=]?%s?=",
	      charset, encoding, encoded_text) == 3) {
      switch(lower_case(encoding)) {
      case "b":
	encoding = "base64"; break;
      case "q":
	encoding = "quoted-printable"; break;
      default:
	throw(({ "invalid rfc1522 encoding "+encoding+"\n", backtrace() }));
      }
      return ({ decode( replace(encoded_text, "_", " "), encoding ),
		  lower_case(charset) });
    } else
      return ({ word, 0 });
  }
  
  string encode_word( array(string) word, string encoding )
  {
    if(!encoding || !word[1])
      return word[0];
    switch(lower_case(encoding)) {
    case "b":
    case "base64":
      encoding = "base64"; break;
    case "q":
    case "quoted-printable":
      encoding = "quoted-printable"; break;
    default:
      throw(({ "invalid rfc1522 encoding "+encoding+"\n", backtrace() }));
    }  
    return "=?"+word[1]+"?"+encoding[0..0]+"?"+
      replace( encode( word[0], encoding ),
	       ({ "?", "_" }), ({ "=3F", "=5F" }))+"?=";
  }

  string guess_subtype(string typ)
  {
    switch(typ) {
    case "text":
      return "plain";
    case "message":
      return "rfc822";
    case "multipart":
      return "mixed";
    }
    return 0;
  }

};

inherit support;

class Message {

  inherit support;
  import Array;

  string encoded_data;
  string decoded_data;
  mapping(string:string) headers;
  array(object) body_parts;

  string type, subtype, charset, boundary, transfer_encoding;
  mapping (string:string) params;

  string disposition;
  mapping (string:string) disp_params;


  string get_filename( )
  {
    return disp_params["filename"] || params["name"];
  }
  
  void setdata( string data )
  {
    if(data != decoded_data) {
      decoded_data = data;
      encoded_data = 0;
    }
  }

  string getdata( )
  {
    if(encoded_data && !decoded_data)
      decoded_data=decode(encoded_data, transfer_encoding);
    return decoded_data;
  }
  
  string getencoded( )
  {
    if(decoded_data && !encoded_data)
      encoded_data=encode(decoded_data, transfer_encoding, get_filename( ));
    return encoded_data;
  }
  
  void setencoding( string encoding )
  {
    if(encoded_data && !decoded_data)
      decoded_data = getdata( );
    headers["content-transfer-encoding"]=transfer_encoding=lower_case(encoding);
    encoded_data = 0;
  }
  
  void setparam( string param, string value )
  {
    param = lower_case(param);
    params[param] = value;
    switch(param) {
    case "charset": charset = value; break;
    case "boundary": boundary = value; break;
    case "name":
      if(transfer_encoding != "x-uue")
	break;
      if(encoded_data && !decoded_data)
	decoded_data = getdata( );
      encoded_data = 0;
      break;
    }
    headers["content-type"] =
      quote(({ type, '/', subtype })+
	    `+(@map(indices(params), lambda(string param) {
	      return ({ ';', param, '=', params[param] });
	    })));
  }
  
  void setdisp_param( string param, string value )
  {
    param = lower_case(param);
    disp_params[param] = value;
    switch(param) {
    case "filename":
      if(transfer_encoding != "x-uue")
	break;
      if(encoded_data && !decoded_data)
	decoded_data = getdata( );
      encoded_data = 0;
      break;
    }
    headers["content-disposition"] =
      quote(({ disposition || "attachment" })+
	    `+(@map(indices(disp_params), lambda(string param) {
	      return ({ ';', param, '=', disp_params[param] });
	    })));
  }
  
  void setcharset( string charset )
  {
    setparam( "charset", charset );
  }
  
  void setboundary( string boundary )
  {
    setparam( "boundary", boundary );
  }
  
  string cast( string dest_type )
  {
    string data;
    object body_part;
    
    if(dest_type != "string")
      throw(({ "can't cast Message to "+dest_type+"\n", backtrace() }));
    
    data = getencoded( );
    
    if(body_parts) {
      
      if(!boundary) {
	if(type != "multipart") { type="multipart"; subtype="mixed"; }
	setboundary(generate_boundary());
      }
      
      data += "\r\n";
      foreach( body_parts, body_part )
	data += "--"+boundary+"\r\n"+((string)body_part)+"\r\n";
      data += "--"+boundary+"--\r\n";
    }
    
    headers["content-length"] = ""+strlen(data);
    
    return map(indices(headers),
	       lambda(string hname){
      return replace(map(hname/"-", String.capitalize)*"-", "Mime", "MIME")+
	": "+headers[hname];
    })*"\r\n"+"\r\n\r\n"+data;
  }

  void create(void | string message,
	      void | mapping(string:string) hdrs,
	      void | array(object) parts)
  {
    encoded_data = 0;
    decoded_data = 0;
    headers = ([ ]);
    params = ([ ]);
    disp_params = ([ ]);
    body_parts = 0;
    type = "text";
    subtype = "plain";
    charset = "us-ascii";
    boundary = 0;
    disposition = 0;
    if (hdrs || parts) {
      string hname;
      if(message)
	decoded_data = message;
      else
	decoded_data = (parts?
			"This is a multi-part message in MIME format.\r\n":
			"");
      if(hdrs)
	foreach( indices(hdrs), hname )
	  headers[lower_case(hname)] = hdrs[hname];
      body_parts = parts;
    } else if (message) {
      string head, body, header, hname, hcontents;
      int mesgsep;
      {
	int mesgsep1 = search(message, "\r\n\r\n");
	int mesgsep2 = search(message, "\n\n");
	mesgsep = (mesgsep1<0? mesgsep2 :
		   (mesgsep2<0? mesgsep1 :
		    (mesgsep1<mesgsep2? mesgsep1 : mesgsep2)));
      }
      if(mesgsep<0) {
	head = message;
	body = "";
      } else {
	head = (mesgsep>0? message[..mesgsep-1]:"");
	body = message[mesgsep+(message[mesgsep]=='\r'? 4:2)..];
      }
      foreach( replace(head, ({"\r", "\n ", "\n\t"}),
		       ({"", " ", " "}))/"\n", header ) {
	if(4==sscanf(header, "%[!-9;-~]%*[ \t]:%*[ \t]%s", hname, hcontents))
	  headers[lower_case(hname)] = hcontents;
      }
      encoded_data = body;
    }
    if(headers["content-type"]) {
      array(array(string|int)) arr =
	tokenize(headers["content-type"]) / ({';'});
      array(string|int) p;
      if(sizeof(arr[0])!=3 || arr[0][1]!='/' ||
	 !stringp(arr[0][0]) || !stringp(arr[0][2]))
	if(sizeof(arr[0])==1 && stringp(arr[0][0]) &&
	   (subtype = guess_subtype(lower_case(type = arr[0][0]))))
	  arr = ({ ({ type, '/', subtype }) }) + arr[1..];
	else
	  throw(({ "invalid Content-Type in message\n", backtrace() }));
      type = lower_case(arr[0][0]);
      subtype = lower_case(arr[0][2]);
      foreach( arr[1..], p ) {
	if(sizeof(p)<3 || p[1]!='=' || !stringp(p[0]))
	  throw(({ "invalid parameter in Content-Type\n", backtrace() }));
	params[ lower_case(p[0]) ] = p[2..]*"";
      }
      charset = lower_case(params["charset"] || charset);
      boundary = params["boundary"];
    }
    if(headers["content-disposition"]) {
      array(array(string|int)) arr =
	tokenize(headers["content-disposition"]) / ({';'});
      array(string|int) p;
      if(sizeof(arr[0])!=1 || !stringp(arr[0][0]))
	throw(({ "invalid Content-Disposition in message\n", backtrace() }));
      disposition = lower_case(arr[0][0]);
      foreach( arr[1..], p ) {
	if(sizeof(p)<3 || p[1]!='=' || !stringp(p[0]))
	  throw(({ "invalid parameter in Content-Disposition\n", backtrace() }));
	disp_params[ lower_case(p[0]) ] = p[2..]*"";
      }
    }
    if(headers["content-transfer-encoding"]) {
      array(string) arr=tokenize(headers["content-transfer-encoding"]);
      if(sizeof(arr)!=1 || !stringp(arr[0]))
	throw(({"invalid Content-Transfer-Encoding in message\n", backtrace()}));
      transfer_encoding = lower_case(arr[0]);
    }
    if(boundary && type=="multipart" && !body_parts &&
       (encoded_data || decoded_data)) {
      array(string) parts = ("\n"+getdata())/("\n--"+boundary);
      if(parts[-1][0..3]!="--\n" && parts[-1][0..3]!="--\r\n")
	throw(({ "multipart message improperly terminated\n", backtrace() }));
      encoded_data = 0;
      decoded_data = parts[0][1..];
      body_parts = map(parts[1..sizeof(parts)-2], lambda(string part){ return object_program(this_object())(part[1..]); });
    }
  }
  
}
