#pike __REAL_VERSION__
#pragma strict_types

//! Various cryptographic classes and functions.
//!
//! @b{Hash functions@}
//! These are based on the @[Hash] API; @[MD2], @[MD4], @[MD5],
//! @[SHA1], @[SHA256].
//!
//! @b{Stream cipher functions@}
//! These are based on the @[Cipher] API; @[AES], @[Arcfour],
//! @[Blowfish], @[CAST], @[DES], @[DES3], @[IDEA], @[Serpent],
//! @[Twofish]. The @[Substitution] program is compatible with the
//! CipherState. Also conforming to the API are the helper programs
//! @[Buffer], @[CBC] and @[Pipe].

#if constant(Nettle)

class HashState {
  inherit Nettle.HashState;
  this_program update(string); // Better return type
}

//! Abstract class for hash algorithms. Contains some tools useful
//! for all hashes.
class Hash
{
  inherit Nettle.HashInfo;

  //! Calling `() will return a @[Nettle.HashState] object.
  HashState `()();
  
  //! @decl string hash(string data)
  //!
  //!  Works as a shortcut for @expr{obj->update(data)->digest()@}.
  //!
  //! @seealso
  //!   @[HashState()->update()] and @[HashState()->digest()].
  string hash(string data)
  {
    return `()()->update(data)->digest();
  }
}

//! Hashes a @[password] together with a @[salt] with the
//! crypt_md5 algorithm and returns the result.
//! @seealso
//!   @[verify_crypt_md5]
string make_crypt_md5(string password, void|string salt) {
  if(salt)
    sscanf(salt, "%s$", salt);
  else
    salt = ([function(string:string)]MIME["encode_base64"])
      (.Random.random_string(8));

  return "$1$"+salt+"$"+Nettle.crypt_md5(password, salt);
}

//! Verifies the @[password] against the crypt_md5 hash.
//! @throws
//!   May throw an exception if the hash value is bad.
//! @seealso
//!   @[make_crypt_md5]
int(0..1) verify_crypt_md5(string password, string hash) {
  string salt;
  if( sscanf(hash, "$1$%s$%s", salt, hash)!=2 )
    error("Error in hash.\n");
  return Nettle.crypt_md5(password, salt)==hash;
}

class CipherState {
  inherit Nettle.CipherState;
  this_program set_encrypt_key(string); // Better return type
  this_program set_decrypt_key(string); // Better return type
}

//! Abstract class for crypto algorithms. Contains some tools useful
//! for all ciphers.
class Cipher
{
  inherit Nettle.CipherInfo;

  //! Calling `() will return a @[Nettle.CipherState] object.
  CipherState `()();

  //! Works as a shortcut for @expr{obj->set_encrypt_key(key)->crypt(data)@}
  string encrypt(string key, string data) {
    return `()()->set_encrypt_key(key)->crypt(data);
  }

  //! Works as a shortcut for @expr{obj->set_decrypt_key(key)->crypt(data)@}
  string decrypt(string key, string data) {
    return `()()->set_decrypt_key(key)->crypt(data);
  }
}

constant CBC = Nettle.CBC;
constant Buffer = Nettle.Proxy;

// Phase out classes. Remove in Pike 7.7.

//! @ignore

#define PHASER(X,Y) static int(0..1) X##whiner; \
class X { \
  static int do_whine = X##whiner?0:(X##whiner = \
    !!Stdio.stderr->write("Crypto." #X " is deprecated. Use Crypto." \
                        #Y " instead.\n")); \
  inherit Crypto.X; \
}

#pike 7.4
PHASER(aes,AES);
PHASER(arcfour,Arcfour);
PHASER(cast,CAST);
PHASER(des,DES);
PHASER(des3,DES3);
PHASER(des_cbc,CBC(Crypto.DES));
PHASER(des3_cbc,CBC(Crypto.DES3));
PHASER(dsa,DSA);
PHASER(hmac,HMAC);
PHASER(idea,IDEA);
PHASER(idea_cbc,CBC(Crypto.IDEA));
PHASER(md2,MD2);
PHASER(md4,MD4);
PHASER(md5,MD5);
PHASER(rijndael,AES);
PHASER(rsa,RSA);
PHASER(sha,SHA1);

//! @endignore


#endif /* constant(Nettle) */
