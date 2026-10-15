
  var bufferView;
  var base64ReverseLookup = new Uint8Array(123/*'z'+1*/);
  for (var i = 25; i >= 0; --i) {
    base64ReverseLookup[48+i] = 52+i; // '0-9'
    base64ReverseLookup[65+i] = i; // 'A-Z'
    base64ReverseLookup[97+i] = 26+i; // 'a-z'
  }
  base64ReverseLookup[43] = 62; // '+'
  base64ReverseLookup[47] = 63; // '/'
  /** @noinline Inlining this function would mean expanding the base64 string 4x times in the source code, which Closure seems to be happy to do. */
  function base64DecodeToExistingUint8Array(uint8Array, offset, b64) {
    var b1, b2, i = 0, j = offset, bLength = b64.length, end = offset + (bLength*3>>2) - (b64[bLength-2] == '=') - (b64[bLength-1] == '=');
    for (; i < bLength; i += 4) {
      b1 = base64ReverseLookup[b64.charCodeAt(i+1)];
      b2 = base64ReverseLookup[b64.charCodeAt(i+2)];
      uint8Array[j++] = base64ReverseLookup[b64.charCodeAt(i)] << 2 | b1 >> 4;
      if (j < end) uint8Array[j++] = b1 << 4 | b2 >> 2;
      if (j < end) uint8Array[j++] = b2 << 6 | base64ReverseLookup[b64.charCodeAt(i+3)];
    }
    return uint8Array;
  }
function initActiveSegments(imports) {
  base64DecodeToExistingUint8Array(bufferView, 16777217, "AQIGBwgMDQ4SExQYGRoeHyA=");
  base64DecodeToExistingUint8Array(bufferView, 16777249, "AQIDBAX//wYHCAkKC///DA0ODxAR/////////////wgBAg==");
  base64DecodeToExistingUint8Array(bufferView, 16777300, "AQAAAAEAAQABAQEAAQIEBggKDA4QEhQUGBgeHgEAAAACAAAAAwAAAAQAAAAFAAAABgAAAAcAAAAIAAAAWQjDBgcGiwUuBeQEpgRxBEMEGgT1A9MDtQOYA34DZQNOAzkDJAMRA/8C7QLcAs0CvQKvAqACkwKGAnkCbQJhAlYCSwJAAjYCLAIiAhgCDwIGAv0B9QHsAeQB3AHUAc0BxQG+AbcBsAGpAaIBmwGVAY8BiAGCAXwBdwFxAWsBZgFgAVsBVQFQAUsBRgFBATwBNwEzAS4BKQElASEBHAEYARQBDwELAQcBAwH/APsA+AD0APAA7ADpAOUA4gDeANsA1wDUANEAzQDKAMcAxADBAL4AuwC4ALUAsgCvAKwAqQCnAKQAoQCfAJwAmQCXAJQAkgCPAI0AigCIAIYAgwCBAH8AfQB6AHgAdgB0AHIAcABuAGwAagBoAGYAZABiAGAAXgBcAFsAWQBXAFUAUwBSAFAATgBNAEsASgBIAEYARQBDAEIAQAA/AD4APAA7ADkAOAA3ADUANAAzADEAMAAvAC4ALQArACoAKQAoACcAJgAlACQAIwAiACEAIAAfAB4AHQAcABsAGgAZABgAFwAXABYAFQAUABQAEwASABEAEQAQAA8ADwAOAA0ADQAMAAwACwAKAAoACQAJAAgACAAHAAcABwAGAAYABQAFAAUABAAEAAQAAwADAAMAAgACAAIAAgABAAEAAQABAAEAAQAB");
  base64DecodeToExistingUint8Array(bufferView, 16777872, "+gf1B+8H6gfkB98H2gfUB88HyQfEB78HuQe0B64HqQekB58HmQeUB48HigeEB38Hegd1B3AHagdlB2AHWwdWB1EHTAdHB0IHPQc4BzMHLgcpByQHHwcaBxUHEAcLBwYHAgf9BvgG8wbuBukG5QbgBtsG1gbSBs0GyAbEBr8Guga1BrEGrAaoBqMGngaaBpUGkQaMBogGgwZ/BnoGdgZxBm0GaAZkBl8GWwZXBlIGTgZJBkUGQQY8BjgGNAYwBisGJwYjBh4GGgYWBhIGDgYJBgUGAQb9BfkF9QXwBewF6AXkBeAF3AXYBdQF0AXMBcgFxAXABbwFuAW0BbAFrAWoBaQFoAWcBZkFlQWRBY0FiQWFBYEFfgV6BXYFcgVvBWsFZwVjBWAFXAVYBVQFUQVNBUkFRgVCBT4FOwU3BTQFMAUsBSkFJQUiBR4FGwUXBRQFEAUMBQkFBgUCBf8E+wT4BPQE8QTtBOoE5wTjBOAE3ATZBNYE0gTPBMwEyATFBMIEvgS7BLgEtQSxBK4EqwSoBKQEoQSeBJsEmASUBJEEjgSLBIgEhQSCBH4EewR4BHUEcgRvBGwEaQRmBGMEYARdBFoEVwRUBFEETgRLBEgERQRCBD8EPAQ5BDYEMwQwBC0EKgQoBCUEIgQfBBwEGQQWBBQEEQQOBAsECAQGBAMEAAQAICgtMDM1Nzg6Ozw9Pj9A");
}
function wasm2js_trap() { throw new Error('abort'); }

  function wasm2js_memory_fill(dest, value, size) {
    dest = dest >>> 0;
    size = size >>> 0;
    if (dest + size > bufferView.length) throw "trap: invalid memory.fill";
    bufferView.fill(value, dest, dest + size);
  }
      
function asmFunc(imports) {
 var buffer = new ArrayBuffer(16842752);
 var HEAP8 = new Int8Array(buffer);
 var HEAP16 = new Int16Array(buffer);
 var HEAP32 = new Int32Array(buffer);
 var HEAPU8 = new Uint8Array(buffer);
 var HEAPU16 = new Uint16Array(buffer);
 var HEAPU32 = new Uint32Array(buffer);
 var HEAPF32 = new Float32Array(buffer);
 var HEAPF64 = new Float64Array(buffer);
 var Math_imul = Math.imul;
 var Math_fround = Math.fround;
 var Math_abs = Math.abs;
 var Math_clz32 = Math.clz32;
 var Math_min = Math.min;
 var Math_max = Math.max;
 var Math_floor = Math.floor;
 var Math_ceil = Math.ceil;
 var Math_trunc = Math.trunc;
 var Math_sqrt = Math.sqrt;
 var global$0 = 16777216;
 function $1() {
  if (HEAP32[4194600]) {
   wasm2js_trap()
  }
  HEAP32[4194600] = 1;
 }
 
 function $2($0) {
  $0 = $0 | 0;
  var $1_1 = 0, $2_1 = 0, $3_1 = 0, $4_1 = 0, $5_1 = 0, $6 = 0;
  wasm2js_memory_fill(16778408, 0, 19520);
  $3_1 = 2;
  while (1) {
   $1_1 = $4_1 + 16778408 | 0;
   HEAP8[$1_1 + 1174 | 0] = $3_1;
   HEAP32[$1_1 + 1148 >> 2] = 16781464;
   HEAP8[$1_1 + 1143 | 0] = 3;
   $2_1 = $1_1 + 1138 | 0;
   HEAP16[$2_1 >> 1] = 511;
   HEAP16[$2_1 + 2 >> 1] = 511;
   HEAP32[$1_1 + 1132 >> 2] = 16781464;
   HEAP32[$1_1 + 1124 >> 2] = 16778408;
   $5_1 = $3_1 - 2 | 0;
   HEAP8[$1_1 + 1118 | 0] = $5_1 + 1;
   HEAP32[$1_1 + 1092 >> 2] = 16781464;
   HEAP8[$1_1 + 1087 | 0] = 3;
   $2_1 = $1_1 + 1082 | 0;
   HEAP16[$2_1 >> 1] = 511;
   HEAP16[$2_1 + 2 >> 1] = 511;
   HEAP32[$1_1 + 1076 >> 2] = 16781464;
   HEAP32[$1_1 + 1068 >> 2] = 16778408;
   HEAP8[$1_1 + 1062 | 0] = $5_1;
   HEAP32[$1_1 + 1036 >> 2] = 16781464;
   HEAP8[$1_1 + 1031 | 0] = 3;
   $2_1 = $1_1 + 1026 | 0;
   HEAP16[$2_1 >> 1] = 511;
   HEAP16[$2_1 + 2 >> 1] = 511;
   HEAP32[$1_1 + 1020 >> 2] = 16781464;
   HEAP32[$1_1 + 1012 >> 2] = 16778408;
   $4_1 = $4_1 + 168 | 0;
   $1_1 = ($3_1 | 0) != 35;
   $3_1 = $3_1 + 3 | 0;
   if ($1_1) {
    continue
   }
   break;
  };
  $5_1 = -1008;
  $3_1 = 0;
  while (1) {
   $1_1 = $5_1 + 16778408 | 0;
   $4_1 = $1_1 + 1008 | 0;
   $2_1 = Math_imul(HEAPU8[$3_1 + 16777216 | 0], 56) + 16779416 | 0;
   HEAP32[$4_1 >> 2] = $2_1;
   HEAP32[$2_1 >> 2] = $4_1;
   $2_1 = $2_1 + 168 | 0;
   HEAP32[$1_1 + 1012 >> 2] = $2_1;
   HEAP32[$2_1 >> 2] = $4_1;
   $2_1 = 3;
   $6 = (($3_1 & 255) >>> 0) % 9 | 0;
   block2 : {
    if ($6 >>> 0 >= 3) {
     $2_1 = -3;
     if ($6 >>> 0 > 5) {
      break block2
     }
    }
    HEAP32[$1_1 + 1016 >> 2] = Math_imul($2_1 + $3_1 | 0, 56) + 16778408;
   }
   HEAP8[$1_1 + 1060 | 0] = $3_1;
   HEAP32[$1_1 + 1052 >> 2] = -1;
   HEAP8[$1_1 + 1040 | 0] = 0;
   HEAP32[$1_1 + 1036 >> 2] = 16781464;
   HEAP32[$1_1 + 1032 >> 2] = 16781464;
   HEAP32[$1_1 + 1028 >> 2] = 16781464;
   HEAP32[$1_1 + 1024 >> 2] = 16781464;
   HEAP32[$1_1 + 1020 >> 2] = 16778408;
   $10($4_1);
   $3_1 = $3_1 + 1 | 0;
   $5_1 = $5_1 + 56 | 0;
   if ($5_1) {
    continue
   }
   break;
  };
  HEAP32[4195365] = 1;
  HEAP8[16781459] = 4;
  HEAP8[16781456] = 1;
  HEAP32[4195373] = ($0 << 10 >>> 0) / 49716;
 }
 
 function $3($0, $1_1) {
  $0 = $0 | 0;
  $1_1 = $1_1 | 0;
  var $2_1 = 0, $3_1 = 0, $4_1 = 0, $5_1 = 0, $6 = 0;
  $3_1 = $0 & 65535;
  $6 = $1_1 & 255;
  $2_1 = HEAP32[4195383];
  $0 = ($2_1 << 4) + 16778408 | 0;
  $4_1 = $0 + 3136 | 0;
  $0 = HEAPU16[$0 + 3144 >> 1];
  block1 : {
   if (!($0 & 512)) {
    $2_1 = $2_1 + 1 & 1023;
    $5_1 = HEAP32[4195380];
    $0 = HEAP32[4195381];
    break block1;
   }
   $8($0 & 511, HEAPU8[$4_1 + 10 | 0]);
   $2_1 = $2_1 + 1 & 1023;
   HEAP32[4195382] = $2_1;
   $1_1 = HEAP32[$4_1 + 4 >> 2];
   $0 = $1_1;
   $5_1 = HEAP32[$4_1 >> 2];
   HEAP32[4195380] = $5_1;
   HEAP32[4195381] = $0;
  }
  HEAP8[$4_1 + 10 | 0] = $6;
  HEAP32[4195383] = $2_1;
  HEAP16[$4_1 + 8 >> 1] = $3_1 | 512;
  $1_1 = HEAP32[4195384] + 2 | 0;
  $2_1 = $1_1;
  $3_1 = HEAP32[4195385];
  $3_1 = $1_1 >>> 0 < 2 ? $3_1 + 1 | 0 : $3_1;
  $1_1 = ($3_1 | 0) == ($0 | 0) & $1_1 >>> 0 > $5_1 >>> 0 | $0 >>> 0 < $3_1 >>> 0;
  $5_1 = $1_1 ? $2_1 : $5_1;
  HEAP32[4195384] = $5_1;
  $0 = $1_1 ? $3_1 : $0;
  HEAP32[4195385] = $0;
  HEAP32[$4_1 >> 2] = $5_1;
  HEAP32[$4_1 + 4 >> 2] = $0;
 }
 
 function $4($0) {
  $0 = $0 | 0;
  var $1_1 = 0, $2_1 = 0, $3_1 = 0, $4_1 = 0, $5_1 = 0, $6 = 0, $7_1 = 0, $8_1 = 0, $9 = 0;
  $7_1 = 16797936;
  $5_1 = global$0 - 16 | 0;
  global$0 = $5_1;
  $8_1 = $0 >>> 0 >= 4096 ? 4096 : $0;
  if ($8_1) {
   while (1) {
    $9 = $5_1 + 8 | 0;
    $0 = HEAP32[4195374];
    $2_1 = HEAP32[4195373];
    if (($0 | 0) >= ($2_1 | 0)) {
     while (1) {
      $0 = HEAP32[4195378];
      HEAP32[4195375] = HEAP32[4195377];
      HEAP32[4195376] = $0;
      $0 = 0;
      $3_1 = 0;
      $2_1 = 0;
      $1_1 = HEAP32[4195368];
      $1_1 = ($1_1 | 0) <= -32768 ? -32768 : $1_1;
      HEAP16[8390755] = ($1_1 | 0) >= 32767 ? 32767 : $1_1;
      $1_1 = HEAP32[4195370];
      $1_1 = ($1_1 | 0) <= -32768 ? -32768 : $1_1;
      HEAP16[8390757] = ($1_1 | 0) >= 32767 ? 32767 : $1_1;
      $7(16779416);
      $7(16779472);
      $7(16779528);
      $7(16779584);
      $7(16779640);
      $7(16779696);
      $7(16779752);
      $7(16779808);
      $7(16779864);
      $7(16779920);
      $7(16779976);
      $7(16780032);
      $7(16780088);
      $7(16780144);
      $7(16780200);
      while (1) {
       $1_1 = $3_1 + 16778408 | 0;
       $4_1 = ((HEAPU16[HEAP32[$1_1 + 20 >> 2] >> 1] + HEAPU16[HEAP32[$1_1 + 16 >> 2] >> 1] | 0) + HEAPU16[HEAP32[$1_1 + 24 >> 2] >> 1] | 0) + HEAPU16[HEAP32[$1_1 + 28 >> 2] >> 1] | 0;
       $2_1 = (($4_1 & HEAPU16[$1_1 + 48 >> 1]) << 16 >> 16) + $2_1 | 0;
       $0 = ((HEAPU16[$1_1 + 44 >> 1] & $4_1) << 16 >> 16) + $0 | 0;
       $3_1 = $3_1 + 56 | 0;
       if (($3_1 | 0) != 1008) {
        continue
       }
       break;
      };
      HEAP32[4195369] = $2_1;
      HEAP32[4195367] = $0;
      $7(16780256);
      $7(16780312);
      $7(16780368);
      $1_1 = HEAP32[4195367];
      $1_1 = ($1_1 | 0) <= -32768 ? -32768 : $1_1;
      HEAP16[8390754] = ($1_1 | 0) >= 32767 ? 32767 : $1_1;
      $1_1 = HEAP32[4195369];
      $1_1 = ($1_1 | 0) <= -32768 ? -32768 : $1_1;
      HEAP16[8390756] = ($1_1 | 0) >= 32767 ? 32767 : $1_1;
      $7(16780424);
      $7(16780480);
      $7(16780536);
      $7(16780592);
      $7(16780648);
      $7(16780704);
      $7(16780760);
      $7(16780816);
      $7(16780872);
      $7(16780928);
      $7(16780984);
      $7(16781040);
      $7(16781096);
      $7(16781152);
      $7(16781208);
      $3_1 = 0;
      $0 = 0;
      $2_1 = 0;
      while (1) {
       $1_1 = $3_1 + 16778408 | 0;
       $4_1 = ((HEAPU16[HEAP32[$1_1 + 20 >> 2] >> 1] + HEAPU16[HEAP32[$1_1 + 16 >> 2] >> 1] | 0) + HEAPU16[HEAP32[$1_1 + 24 >> 2] >> 1] | 0) + HEAPU16[HEAP32[$1_1 + 28 >> 2] >> 1] | 0;
       $2_1 = (($4_1 & HEAPU16[$1_1 + 50 >> 1]) << 16 >> 16) + $2_1 | 0;
       $0 = ((HEAPU16[$1_1 + 46 >> 1] & $4_1) << 16 >> 16) + $0 | 0;
       $3_1 = $3_1 + 56 | 0;
       if (($3_1 | 0) != 1008) {
        continue
       }
       break;
      };
      HEAP32[4195370] = $2_1;
      HEAP32[4195368] = $0;
      $7(16781264);
      $7(16781320);
      $7(16781376);
      $1_1 = HEAPU8[16781458];
      $3_1 = HEAPU16[8390716];
      if (($3_1 & 63) == 63) {
       $1_1 = $1_1 + (($1_1 & 255) >>> 0 < 209 ? 1 : 47) | 0;
       HEAP8[16781458] = $1_1;
      }
      $1_1 = $1_1 & 255;
      block2 : {
       if ($1_1 >>> 0 <= 104) {
        $0 = $1_1 >>> HEAPU8[16781459] | 0;
        break block2;
       }
       $0 = 210 - $1_1 >> HEAPU8[16781459];
      }
      HEAP8[16781457] = $0;
      if (($3_1 & 1023) == 1023) {
       HEAP8[16781455] = HEAPU8[16781455] + 1 & 7
      }
      $0 = 1;
      HEAP16[8390716] = $3_1 + 1;
      $1_1 = HEAPU8[16781449];
      block18 : {
       block17 : {
        if ($1_1) {
         $2_1 = HEAP32[4195361];
         $4_1 = HEAP32[4195360];
         $3_1 = $4_1;
         block5 : {
          if ($4_1 & 1) {
           break block5
          }
          if ($4_1 & 2) {
           $0 = 2;
           break block5;
          }
          if ($4_1 & 4) {
           $0 = 3;
           break block5;
          }
          if ($4_1 & 8) {
           $0 = 4;
           break block5;
          }
          if ($4_1 & 16) {
           $0 = 5;
           break block5;
          }
          if ($4_1 & 32) {
           $0 = 6;
           break block5;
          }
          if ($4_1 & 64) {
           $0 = 7;
           break block5;
          }
          if ($4_1 & 128) {
           $0 = 8;
           break block5;
          }
          if ($4_1 & 256) {
           $0 = 9;
           break block5;
          }
          if ($4_1 & 512) {
           $0 = 10;
           break block5;
          }
          if ($4_1 & 1024) {
           $0 = 11;
           break block5;
          }
          if ($4_1 & 2048) {
           $0 = 12;
           break block5;
          }
          $0 = $4_1 & 4096 ? 13 : 0;
         }
         HEAP8[16781450] = $0;
         HEAP8[16781451] = $3_1 & 3;
         break block17;
        }
        if (!HEAPU8[16781448]) {
         break block18
        }
        $4_1 = HEAP32[4195360];
        $2_1 = HEAP32[4195361];
       }
       $3_1 = ($4_1 | 0) == -1 & ($2_1 | 0) == 15;
       HEAP8[16781448] = $3_1;
       $0 = $4_1 + 1 | 0;
       $2_1 = $0 ? $2_1 : $2_1 + 1 | 0;
       HEAP32[4195360] = $3_1 ? 0 : $0;
       HEAP32[4195361] = $3_1 ? 0 : $2_1;
      }
      HEAP8[16781449] = $1_1 ^ 1;
      $1_1 = HEAP32[4195382];
      $0 = ($1_1 << 4) + 16781544 | 0;
      $3_1 = HEAP32[$0 >> 2];
      $6 = HEAP32[$0 + 4 >> 2];
      $2_1 = HEAP32[4195381];
      $4_1 = HEAP32[4195380];
      block19 : {
       if (($6 | 0) == ($2_1 | 0) & $4_1 >>> 0 < $3_1 >>> 0 | $2_1 >>> 0 < $6 >>> 0) {
        break block19
       }
       $3_1 = HEAPU16[$0 + 8 >> 1];
       if (!($3_1 & 512)) {
        break block19
       }
       $0 = $0 + 8 | 0;
       while (1) {
        $3_1 = $3_1 & 511;
        HEAP16[$0 >> 1] = $3_1;
        $8($3_1, HEAPU8[($1_1 << 4) + 16781554 | 0]);
        $1_1 = HEAP32[4195382] + 1 & 1023;
        HEAP32[4195382] = $1_1;
        $3_1 = ($1_1 << 4) + 16781544 | 0;
        $2_1 = $3_1;
        $0 = HEAP32[$2_1 >> 2];
        $6 = HEAP32[$2_1 + 4 >> 2];
        $2_1 = HEAP32[4195381];
        $4_1 = HEAP32[4195380];
        if (($6 | 0) == ($2_1 | 0) & $0 >>> 0 > $4_1 >>> 0 | $6 >>> 0 > $2_1 >>> 0) {
         break block19
        }
        $0 = $3_1 + 8 | 0;
        $3_1 = HEAPU16[$3_1 + 8 >> 1];
        if ($3_1 & 512) {
         continue
        }
        break;
       };
      }
      $4_1 = $4_1 + 1 | 0;
      HEAP32[4195380] = $4_1;
      $0 = $2_1;
      HEAP32[4195381] = $4_1 ? $0 : $0 + 1 | 0;
      $2_1 = HEAP32[4195373];
      $0 = HEAP32[4195374] - $2_1 | 0;
      HEAP32[4195374] = $0;
      if (($0 | 0) >= ($2_1 | 0)) {
       continue
      }
      break;
     }
    }
    $1_1 = $2_1 - $0 | 0;
    HEAP16[$9 >> 1] = (Math_imul(HEAP16[8390754], $0) + Math_imul($1_1, HEAP16[8390750]) | 0) / ($2_1 | 0);
    HEAP16[$9 + 2 >> 1] = (Math_imul(HEAP16[8390755], $0) + Math_imul(HEAP16[8390751], $1_1) | 0) / ($2_1 | 0);
    HEAP16[$9 + 4 >> 1] = (Math_imul(HEAP16[8390756], $0) + Math_imul(HEAP16[8390752], $1_1) | 0) / ($2_1 | 0);
    HEAP32[4195374] = $0 + 1024;
    HEAP16[$9 + 6 >> 1] = (Math_imul(HEAP16[8390757], $0) + Math_imul(HEAP16[8390753], $1_1) | 0) / ($2_1 | 0);
    $0 = HEAPU16[$5_1 + 8 >> 1] | HEAPU16[$5_1 + 10 >> 1] << 16;
    HEAP16[$7_1 >> 1] = $0;
    HEAP16[$7_1 + 2 >> 1] = $0 >>> 16;
    $7_1 = $7_1 + 4 | 0;
    $8_1 = $8_1 - 1 | 0;
    if ($8_1) {
     continue
    }
    break;
   }
  }
  global$0 = $5_1 + 16 | 0;
  return 16797936;
 }
 
 function $5() {
  return 4096;
 }
 
 function $7($0) {
  var $1_1 = 0, $2_1 = 0, $3_1 = 0, $4_1 = 0, $5_1 = 0, $6 = 0, $7_1 = 0, $8_1 = 0, $9 = 0, $10_1 = 0, $11 = 0, $12 = 0, wasm2js_i32$0 = 0, wasm2js_i32$1 = 0;
  $6 = HEAP32[$0 >> 2];
  $4_1 = HEAPU8[$6 + 40 | 0];
  block1 : {
   if ($4_1) {
    $2_1 = HEAP16[$0 + 8 >> 1];
    $4_1 = $2_1 + HEAP16[$0 + 16 >> 1] >> 9 - $4_1;
    break block1;
   }
   $2_1 = HEAPU16[$0 + 8 >> 1];
   $4_1 = 0;
  }
  HEAP16[$0 + 16 >> 1] = $2_1;
  HEAP16[$0 + 10 >> 1] = $4_1;
  $1_1 = HEAPU16[$0 + 18 >> 1];
  HEAP16[$0 + 20 >> 1] = HEAPU8[HEAP32[$0 + 28 >> 2]] + ($1_1 + (HEAPU8[$0 + 37 | 0] << 2) + (HEAPU8[$0 + 25 | 0] >>> HEAPU8[HEAPU8[$0 + 36 | 0] + 16777280 | 0]) | 0);
  $10_1 = HEAPU8[$0 + 43 | 0];
  $9 = HEAPU8[$0 + 23 | 0];
  $5_1 = $9;
  $4_1 = !$10_1 | ($5_1 | 0) != 3;
  block8 : {
   block3 : {
    if (!$4_1) {
     $2_1 = 1;
     $3_1 = HEAPU8[$0 + 38 | 0];
     break block3;
    }
    $2_1 = 0;
    block7 : {
     switch ($5_1 | 0) {
     case 0:
      $3_1 = HEAPU8[$0 + 38 | 0];
      $9 = 0;
      break block8;
     case 1:
      $3_1 = HEAPU8[$0 + 39 | 0];
      $9 = 1;
      break block8;
     case 2:
      $9 = 2;
      if (HEAPU8[$0 + 33 | 0]) {
       break block8
      }
      $3_1 = HEAPU8[$0 + 41 | 0];
      break block8;
     case 3:
      break block7;
     default:
      break block8;
     };
    }
    $3_1 = HEAPU8[$0 + 41 | 0];
   }
   $9 = 3;
  }
  HEAP32[$0 + 44 >> 2] = $2_1;
  $8_1 = HEAPU8[$6 + 43 | 0] >>> ((HEAPU8[$0 + 34 | 0] ^ 1) << 1) | 0;
  $2_1 = $8_1 + ($3_1 << 2) | 0;
  $5_1 = $2_1 & 64 ? 15 : ($2_1 & 252) >>> 2 | 0;
  $2_1 = HEAP32[$0 + 4 >> 2];
  $12 = $4_1 ^ 1;
  block10 : {
   if (!($3_1 & 255)) {
    break block10
   }
   if ($5_1 >>> 0 <= 11) {
    if (!HEAPU8[$2_1 + 3041 | 0]) {
     break block10
    }
    block14 : {
     switch ((HEAPU8[$2_1 + 3042 | 0] + $5_1 & 255) - 12 | 0) {
     case 0:
      $7_1 = 1;
      break block10;
     case 1:
      $7_1 = $8_1 >>> 1 & 1;
      break block10;
     case 2:
      break block14;
     default:
      break block10;
     };
    }
    $7_1 = $8_1 & 1;
    break block10;
   }
   $3_1 = HEAPU8[(HEAPU8[$2_1 + 3043 | 0] + (($8_1 & 3) << 2) | 0) + 16777296 | 0] + ($5_1 & 3) | 0;
   $7_1 = $3_1 & 4 ? 3 : $3_1;
   if ($7_1 & 255) {
    break block10
   }
   $7_1 = HEAPU8[$2_1 + 3041 | 0];
  }
  $3_1 = $4_1 ? $1_1 : ($5_1 & 255) != 15 ? $1_1 : 0;
  $8_1 = $1_1 & 504;
  $3_1 = ($8_1 | 0) != 504 ? $3_1 : $9 ? ($12 ? $3_1 : 511) : $3_1;
  block18 : {
   block17 : {
    switch ($9 | 0) {
    case 0:
     if (!$1_1) {
      HEAP8[$0 + 23 | 0] = 1;
      break block18;
     }
     if (!$10_1) {
      break block18
     }
     if (!($7_1 & 255)) {
      break block18
     }
     if (($5_1 | 0) == 15) {
      break block18
     }
     $11 = $1_1 >>> 4 - ($7_1 & 255) ^ -1;
     break block18;
    case 1:
     if (HEAPU8[$0 + 40 | 0] == ($1_1 >>> 4 | 0)) {
      HEAP8[$0 + 23 | 0] = 2;
      break block18;
     }
     if ($4_1 ^ 1 | ($8_1 | 0) == 504) {
      break block18
     }
     $1_1 = $7_1 & 255;
     if (!$1_1) {
      break block18
     }
     $11 = 1 << $1_1 - 1;
     break block18;
    case 2:
    case 3:
     break block17;
    default:
     break block18;
    };
   }
   if ($4_1 ^ 1 | ($8_1 | 0) == 504) {
    break block18
   }
   $1_1 = $7_1 & 255;
   if (!$1_1) {
    break block18
   }
   $11 = 1 << $1_1 - 1;
  }
  HEAP16[$0 + 18 >> 1] = $3_1 + $11 & 511;
  if (!(($12 ^ 1) & ($10_1 | 0) != 0)) {
   HEAP8[$0 + 23 | 0] = $10_1 ? 0 : 3
  }
  $3_1 = HEAPU16[$6 + 34 >> 1];
  if (HEAPU8[$0 + 32 | 0]) {
   $1_1 = HEAPU8[$2_1 + 3047 | 0];
   $5_1 = ($1_1 & 3 ? ($3_1 >>> 7 & 7) >>> ($1_1 & 1) | 0 : 0) >>> HEAPU8[$2_1 + 3048 | 0] | 0;
   $3_1 = ($1_1 & 4 ? 0 - $5_1 | 0 : $5_1) + $3_1 | 0;
  }
  $1_1 = HEAP32[$0 + 48 >> 2];
  HEAP16[$0 + 52 >> 1] = $1_1 >>> 9;
  HEAP32[$0 + 48 >> 2] = (Math_imul(HEAPU8[HEAPU8[$0 + 35 | 0] + 16777312 | 0], ($3_1 & 65535) << HEAPU8[$6 + 38 | 0] >> 1) >>> 1 | 0) + ($4_1 ? $1_1 : 0);
  block24 : {
   block26 : {
    block25 : {
     block23 : {
      $6 = HEAPU8[$0 + 54 | 0];
      switch ($6 - 13 | 0) {
      case 0:
       break block23;
      case 4:
       break block25;
      default:
       break block24;
      };
     }
     HEAP8[$2_1 + 3078 | 0] = $1_1 >>> 16 & 1;
     HEAP8[$2_1 + 3077 | 0] = $1_1 >>> 12 & 1;
     HEAP8[$2_1 + 3076 | 0] = $1_1 >>> 11 & 1;
     $4_1 = 3079;
     $3_1 = 17;
     break block26;
    }
    if (!(HEAPU8[$2_1 + 3046 | 0] & 32)) {
     break block24
    }
    HEAP8[$2_1 + 3080 | 0] = $1_1 >>> 12 & 1;
    $4_1 = 3081;
    $3_1 = 14;
   }
   HEAP8[$2_1 + $4_1 | 0] = $1_1 >>> $3_1 & 1;
  }
  $4_1 = HEAP32[$2_1 + 3052 >> 2];
  block27 : {
   if (!(HEAPU8[$2_1 + 3046 | 0] & 32)) {
    break block27
   }
   $1_1 = HEAPU8[$2_1 + 3081 | 0];
   $1_1 = $1_1 ^ HEAPU8[$2_1 + 3077 | 0] | HEAPU8[$2_1 + 3078 | 0] ^ HEAPU8[$2_1 + 3076 | 0] | $1_1 ^ HEAPU8[$2_1 + 3080 | 0];
   block32 : {
    block30 : {
     switch ($6 - 13 | 0) {
     case 0:
      $6 = $1_1 << 9;
      $3_1 = $6 | 208;
      if (($4_1 & 1) != ($1_1 & 255)) {
       break block32
      }
      $3_1 = $6 | 52;
      break block32;
     case 3:
      $1_1 = HEAPU8[$2_1 + 3079 | 0];
      $3_1 = ($1_1 ^ $4_1 & 1) << 8 | $1_1 << 9;
      break block32;
     case 4:
      break block30;
     default:
      break block27;
     };
    }
    $3_1 = $1_1 << 9 | 128;
   }
   HEAP16[$0 + 52 >> 1] = $3_1;
  }
  HEAP32[$2_1 + 3052 >> 2] = ($4_1 ^ $4_1 >>> 14) << 22 & 4194304 | $4_1 >>> 1;
  (wasm2js_i32$0 = $0, wasm2js_i32$1 = FUNCTION_TABLE[HEAP32[(HEAPU8[$0 + 42 | 0] << 2) + 16777328 >> 2]](HEAPU16[HEAP32[$0 + 12 >> 2] >> 1] + HEAPU16[$0 + 52 >> 1] & 65535, HEAPU16[$0 + 20 >> 1]) | 0), HEAP16[wasm2js_i32$0 + 8 >> 1] = wasm2js_i32$1;
 }
 
 function $8($0, $1_1) {
  var $2_1 = 0, $3_1 = 0, $4_1 = 0, $5_1 = 0, $6 = 0, $7_1 = 0, $8_1 = 0, $9 = 0;
  $3_1 = $0 >>> 8 & 1;
  block1 : {
   block8 : {
    block7 : {
     block6 : {
      block9 : {
       block5 : {
        block4 : {
         block3 : {
          block2 : {
           block : {
            $0 = $0 & 255;
            switch ($0 >>> 4 | 0) {
            case 0:
             break block;
            case 2:
            case 3:
             break block2;
            case 4:
            case 5:
             break block3;
            case 6:
            case 7:
             break block4;
            case 8:
            case 9:
             break block5;
            case 10:
             break block6;
            case 11:
             break block7;
            case 12:
             break block8;
            case 14:
            case 15:
             break block9;
            default:
             break block1;
            };
           }
           $0 = $0 & 15;
           if ($3_1) {
            block12 : {
             switch ($0 - 4 | 0) {
             case 0:
              $3_1 = 0;
              while (1) {
               $2_1 = ($3_1 >>> 0 > 2 ? $8_1 + 6 | 0 : $3_1) & 255;
               $0 = $2_1 + 3 | 0;
               $7_1 = Math_imul($2_1, 56) + 16778408 | 0;
               block10 : {
                if ($1_1 >>> $3_1 & 1) {
                 $2_1 = HEAP32[$7_1 + 8 >> 2];
                 $6 = HEAPU8[$7_1 + 38 | 0];
                 HEAP8[$2_1 + 38 | 0] = $6;
                 $5_1 = HEAPU16[$7_1 + 34 >> 1];
                 HEAP16[$2_1 + 34 >> 1] = $5_1;
                 $4_1 = HEAP32[$2_1 >> 2];
                 $9 = $4_1;
                 $4_1 = HEAP32[$4_1 >> 2];
                 $4_1 = ((HEAPU8[(HEAPU16[$4_1 + 34 >> 1] >>> 6 | 0) + 16778384 | 0] << 2) + (HEAPU8[$4_1 + 38 | 0] << 5) | 0) - 256 | 0;
                 HEAP8[$9 + 25 | 0] = ($4_1 | 0) > 0 ? $4_1 : 0;
                 HEAP8[$7_1 + 32 | 0] = 1;
                 HEAP8[(Math_imul($0, 56) + 16778408 | 0) + 32 | 0] = 2;
                 HEAP8[$2_1 + 43 | 0] = $5_1 >>> 9 - HEAPU8[HEAP32[$2_1 + 12 >> 2] + 3045 | 0] & 1 | $6 << 1;
                 $0 = HEAP32[$2_1 + 4 >> 2];
                 $4_1 = $0;
                 $0 = HEAP32[$0 >> 2];
                 $0 = ((HEAPU8[(HEAPU16[$0 + 34 >> 1] >>> 6 | 0) + 16778384 | 0] << 2) + (HEAPU8[$0 + 38 | 0] << 5) | 0) - 256 | 0;
                 HEAP8[$4_1 + 25 | 0] = ($0 | 0) > 0 ? $0 : 0;
                 HEAP8[$2_1 + 42 | 0] = HEAPU8[$2_1 + 41 | 0] | HEAPU8[$7_1 + 41 | 0] << 1 | 4;
                 HEAP8[$7_1 + 42 | 0] = 8;
                 break block10;
                }
                $2_1 = Math_imul($0, 56) + 16778408 | 0;
                $0 = HEAPU8[$2_1 + 39 | 0];
                HEAP8[$2_1 + 38 | 0] = $0;
                $6 = HEAPU16[$2_1 + 36 >> 1];
                HEAP16[$2_1 + 34 >> 1] = $6;
                $5_1 = HEAP32[$2_1 >> 2];
                $4_1 = $5_1;
                $5_1 = HEAP32[$5_1 >> 2];
                $5_1 = ((HEAPU8[(HEAPU16[$5_1 + 34 >> 1] >>> 6 | 0) + 16778384 | 0] << 2) + (HEAPU8[$5_1 + 38 | 0] << 5) | 0) - 256 | 0;
                HEAP8[$4_1 + 25 | 0] = ($5_1 | 0) > 0 ? $5_1 : 0;
                HEAP8[$7_1 + 32 | 0] = 0;
                HEAP8[$2_1 + 32 | 0] = 0;
                HEAP8[$2_1 + 43 | 0] = $6 >>> 9 - HEAPU8[HEAP32[$2_1 + 12 >> 2] + 3045 | 0] & 1 | $0 << 1;
                $0 = HEAP32[$2_1 + 4 >> 2];
                $4_1 = $0;
                $0 = HEAP32[$0 >> 2];
                $0 = ((HEAPU8[(HEAPU16[$0 + 34 >> 1] >>> 6 | 0) + 16778384 | 0] << 2) + (HEAPU8[$0 + 38 | 0] << 5) | 0) - 256 | 0;
                HEAP8[$4_1 + 25 | 0] = ($0 | 0) > 0 ? $0 : 0;
                HEAP8[$7_1 + 42 | 0] = HEAPU8[$7_1 + 41 | 0];
                $10($7_1);
                $7_1 = HEAPU8[$2_1 + 41 | 0];
                HEAP8[$2_1 + 42 | 0] = $7_1;
                block31 : {
                 switch (HEAPU8[$2_1 + 32 | 0] - 1 | 0) {
                 case 0:
                  $0 = HEAP32[$2_1 + 8 >> 2];
                  HEAP8[$0 + 42 | 0] = HEAPU8[$0 + 41 | 0] | $7_1 << 1 | 4;
                  HEAP8[$2_1 + 42 | 0] = 8;
                  $2_1 = $0;
                  break block10;
                 case 1:
                  break block31;
                 default:
                  break block10;
                 };
                }
                $0 = HEAP32[$2_1 + 8 >> 2];
                HEAP8[$2_1 + 42 | 0] = HEAPU8[$0 + 41 | 0] << 1 | $7_1 | 4;
                HEAP8[$0 + 42 | 0] = 8;
               }
               $10($2_1);
               $8_1 = $8_1 + 1 | 0;
               $3_1 = $3_1 + 1 | 0;
               if (($3_1 | 0) != 6) {
                continue
               }
               break;
              };
              return;
             case 1:
              break block12;
             default:
              break block1;
             };
            }
            HEAP8[16781452] = $1_1 & 1;
            return;
           }
           if (($0 | 0) != 8) {
            break block1
           }
           HEAP8[16781453] = $1_1 >>> 6 & 1;
           return;
          }
          if (!(1 << $0 & 4144959)) {
           break block1
          }
          $0 = Math_imul(HEAP8[($0 & 31) + 16777248 | 0] + Math_imul($3_1, 18) | 0, 56) + 16778408 | 0;
          HEAP8[$0 + 1043 | 0] = $1_1 & 15;
          HEAP8[$0 + 1042 | 0] = $1_1 >>> 4 & 1;
          HEAP8[$0 + 1041 | 0] = $1_1 >>> 5 & 1;
          HEAP8[$0 + 1040 | 0] = $1_1 >>> 6 & 1;
          HEAP32[$0 + 1036 >> 2] = HEAP32[$0 + 1012 >> 2] + ($1_1 << 24 >> 24 >= 0 ? 3056 : 3049);
          return;
         }
         if (!(1 << $0 & 4144959)) {
          break block1
         }
         $0 = Math_imul(HEAP8[($0 & 31) + 16777248 | 0] + Math_imul($3_1, 18) | 0, 56) + 16778408 | 0;
         HEAP8[$0 + 1045 | 0] = $1_1 & 63;
         HEAP8[$0 + 1044 | 0] = $1_1 >>> 6;
         $1_1 = HEAP32[$0 + 1008 >> 2];
         $1_1 = ((HEAPU8[(HEAPU16[$1_1 + 34 >> 1] >>> 6 | 0) + 16778384 | 0] << 2) + (HEAPU8[$1_1 + 38 | 0] << 5) | 0) - 256 | 0;
         HEAP8[$0 + 1033 | 0] = ($1_1 | 0) > 0 ? $1_1 : 0;
         return;
        }
        if (!(1 << $0 & 4144959)) {
         break block1
        }
        $0 = Math_imul(HEAP8[($0 & 31) + 16777248 | 0] + Math_imul($3_1, 18) | 0, 56) + 16778408 | 0;
        HEAP8[$0 + 1047 | 0] = $1_1 & 15;
        HEAP8[$0 + 1046 | 0] = $1_1 >>> 4;
        return;
       }
       if (!(1 << $0 & 4144959)) {
        break block1
       }
       $0 = Math_imul(HEAP8[($0 & 31) + 16777248 | 0] + Math_imul($3_1, 18) | 0, 56) + 16778408 | 0;
       HEAP8[$0 + 1049 | 0] = $1_1 & 15;
       $1_1 = $1_1 >>> 4 | 0;
       HEAP8[$0 + 1048 | 0] = ($1_1 | 0) == 15 ? 31 : $1_1;
       return;
      }
      if (!(1 << $0 & 4144959)) {
       break block1
      }
      $0 = Math_imul(HEAP8[($0 & 31) + 16777248 | 0] + Math_imul($3_1, 18) | 0, 56) + 16778408 | 0;
      HEAP8[$0 + 1050 | 0] = $1_1 & 7;
      if (HEAPU8[HEAP32[$0 + 1012 >> 2] + 3044 | 0]) {
       break block1
      }
      HEAP8[($0 + 1008 | 0) + 42 | 0] = $1_1 & 3;
      return;
     }
     $0 = $0 & 15;
     if ($0 >>> 0 > 8) {
      break block1
     }
     $0 = Math_imul(Math_imul($3_1, 9) + $0 | 0, 56) + 16778408 | 0;
     $1_1 = $1_1 | HEAPU16[$0 + 36 >> 1] & 768;
     HEAP16[$0 + 36 >> 1] = $1_1;
     $2_1 = HEAPU8[$0 + 32 | 0];
     if (($2_1 | 0) == 2) {
      break block1
     }
     $3_1 = HEAPU8[$0 + 39 | 0];
     HEAP8[$0 + 38 | 0] = $3_1;
     HEAP16[$0 + 34 >> 1] = $1_1;
     $6 = HEAP32[$0 >> 2];
     $4_1 = $6;
     $6 = HEAP32[$6 >> 2];
     $6 = ((HEAPU8[(HEAPU16[$6 + 34 >> 1] >>> 6 | 0) + 16778384 | 0] << 2) + (HEAPU8[$6 + 38 | 0] << 5) | 0) - 256 | 0;
     HEAP8[$4_1 + 25 | 0] = ($6 | 0) > 0 ? $6 : 0;
     $8_1 = $3_1 << 1;
     HEAP8[$0 + 43 | 0] = $8_1 | $1_1 >>> 9 - HEAPU8[HEAP32[$0 + 12 >> 2] + 3045 | 0] & 1;
     $6 = HEAP32[$0 + 4 >> 2];
     $4_1 = $6;
     $6 = HEAP32[$6 >> 2];
     $6 = ((HEAPU8[(HEAPU16[$6 + 34 >> 1] >>> 6 | 0) + 16778384 | 0] << 2) + (HEAPU8[$6 + 38 | 0] << 5) | 0) - 256 | 0;
     HEAP8[$4_1 + 25 | 0] = ($6 | 0) > 0 ? $6 : 0;
     if (($2_1 | 0) != 1) {
      break block1
     }
     $0 = HEAP32[$0 + 8 >> 2];
     HEAP8[$0 + 38 | 0] = $3_1;
     HEAP16[$0 + 34 >> 1] = $1_1;
     $2_1 = HEAP32[$0 >> 2];
     $4_1 = $2_1;
     $2_1 = HEAP32[$2_1 >> 2];
     $2_1 = ((HEAPU8[(HEAPU16[$2_1 + 34 >> 1] >>> 6 | 0) + 16778384 | 0] << 2) + (HEAPU8[$2_1 + 38 | 0] << 5) | 0) - 256 | 0;
     HEAP8[$4_1 + 25 | 0] = ($2_1 | 0) > 0 ? $2_1 : 0;
     HEAP8[$0 + 43 | 0] = $8_1 | $1_1 >>> 9 - HEAPU8[HEAP32[$0 + 12 >> 2] + 3045 | 0] & 1;
     $0 = HEAP32[$0 + 4 >> 2];
     $1_1 = $0;
     $0 = HEAP32[$0 >> 2];
     $0 = ((HEAPU8[(HEAPU16[$0 + 34 >> 1] >>> 6 | 0) + 16778384 | 0] << 2) + (HEAPU8[$0 + 38 | 0] << 5) | 0) - 256 | 0;
     HEAP8[$1_1 + 25 | 0] = ($0 | 0) > 0 ? $0 : 0;
     return;
    }
    block13 : {
     if (($0 | 0) != 189) {
      break block13
     }
     if ($3_1) {
      break block13
     }
     HEAP8[16781454] = $1_1 & 63;
     $0 = $1_1 >>> 6 ^ -1;
     HEAP8[16781456] = $0 & 1;
     HEAP8[16781459] = ($0 & 2) + 2;
     if (!($1_1 & 32)) {
      HEAP8[16778776] = 0;
      $10(16778744);
      $0 = HEAP32[4194686];
      HEAP8[$0 + 43 | 0] = HEAPU8[$0 + 43 | 0] & 253;
      HEAP8[16778832] = 0;
      $0 = HEAP32[4194687];
      HEAP8[$0 + 43 | 0] = HEAPU8[$0 + 43 | 0] & 253;
      $10(16778800);
      $0 = HEAP32[4194700];
      HEAP8[$0 + 43 | 0] = HEAPU8[$0 + 43 | 0] & 253;
      HEAP8[16778888] = 0;
      $0 = HEAP32[4194701];
      HEAP8[$0 + 43 | 0] = HEAPU8[$0 + 43 | 0] & 253;
      $10(16778856);
      $0 = HEAP32[4194714];
      HEAP8[$0 + 43 | 0] = HEAPU8[$0 + 43 | 0] & 253;
      $0 = HEAP32[4194715];
      HEAP8[$0 + 43 | 0] = HEAPU8[$0 + 43 | 0] & 253;
      return;
     }
     HEAP8[16778888] = 3;
     HEAP8[16778832] = 3;
     HEAP8[16778776] = 3;
     HEAP32[4194693] = 16781464;
     HEAP32[4194692] = 16781464;
     $0 = HEAP32[4194687] + 8 | 0;
     HEAP32[4194691] = $0;
     HEAP32[4194690] = $0;
     $0 = HEAP32[4194700] + 8 | 0;
     HEAP32[4194705] = $0;
     HEAP32[4194704] = $0;
     $0 = HEAP32[4194701] + 8 | 0;
     HEAP32[4194707] = $0;
     HEAP32[4194706] = $0;
     $0 = HEAP32[4194714] + 8 | 0;
     HEAP32[4194719] = $0;
     HEAP32[4194718] = $0;
     $0 = HEAP32[4194715] + 8 | 0;
     HEAP32[4194721] = $0;
     HEAP32[4194720] = $0;
     $10(16778744);
     $10(16778800);
     $10(16778856);
     $0 = HEAP32[4194700];
     $1_1 = $0;
     $2_1 = HEAPU8[$0 + 43 | 0] & 253;
     $0 = HEAPU8[16781454];
     HEAP8[$1_1 + 43 | 0] = $2_1 | $0 << 1 & 2;
     $1_1 = HEAP32[4194715];
     HEAP8[$1_1 + 43 | 0] = HEAPU8[$1_1 + 43 | 0] & 253 | $0 & 2;
     $1_1 = HEAP32[4194714];
     HEAP8[$1_1 + 43 | 0] = HEAPU8[$1_1 + 43 | 0] & 253 | $0 >>> 1 & 2;
     $1_1 = HEAP32[4194701];
     HEAP8[$1_1 + 43 | 0] = HEAPU8[$1_1 + 43 | 0] & 253 | $0 >>> 2 & 2;
     $1_1 = HEAP32[4194686];
     $3_1 = HEAPU8[$1_1 + 43 | 0];
     if ($0 & 16) {
      HEAP8[$1_1 + 43 | 0] = $3_1 | 2;
      $0 = HEAP32[4194687];
      HEAP8[$0 + 43 | 0] = HEAPU8[$0 + 43 | 0] | 2;
      return;
     }
     HEAP8[$1_1 + 43 | 0] = $3_1 & 253;
     $0 = HEAP32[4194687];
     HEAP8[$0 + 43 | 0] = HEAPU8[$0 + 43 | 0] & 253;
     return;
    }
    $0 = $0 & 15;
    if ($0 >>> 0 > 8) {
     break block1
    }
    $0 = Math_imul(Math_imul($3_1, 9) + $0 | 0, 56) + 16778408 | 0;
    $3_1 = $1_1 >>> 2 & 7;
    HEAP8[$0 + 39 | 0] = $3_1;
    $2_1 = HEAPU8[$0 + 36 | 0] | $1_1 << 8 & 768;
    HEAP16[$0 + 36 >> 1] = $2_1;
    $8_1 = HEAPU8[$0 + 32 | 0];
    block14 : {
     if (($8_1 | 0) == 2) {
      break block14
     }
     HEAP8[$0 + 38 | 0] = $3_1;
     HEAP16[$0 + 34 >> 1] = $2_1;
     $4_1 = HEAP32[$0 >> 2];
     $5_1 = $4_1;
     $4_1 = HEAP32[$4_1 >> 2];
     $4_1 = ((HEAPU8[(HEAPU16[$4_1 + 34 >> 1] >>> 6 | 0) + 16778384 | 0] << 2) + (HEAPU8[$4_1 + 38 | 0] << 5) | 0) - 256 | 0;
     HEAP8[$5_1 + 25 | 0] = ($4_1 | 0) > 0 ? $4_1 : 0;
     $6 = $3_1 << 1;
     HEAP8[$0 + 43 | 0] = $6 | $2_1 >>> 9 - HEAPU8[HEAP32[$0 + 12 >> 2] + 3045 | 0] & 1;
     $4_1 = HEAP32[$0 + 4 >> 2];
     $5_1 = $4_1;
     $4_1 = HEAP32[$4_1 >> 2];
     $4_1 = ((HEAPU8[(HEAPU16[$4_1 + 34 >> 1] >>> 6 | 0) + 16778384 | 0] << 2) + (HEAPU8[$4_1 + 38 | 0] << 5) | 0) - 256 | 0;
     HEAP8[$5_1 + 25 | 0] = ($4_1 | 0) > 0 ? $4_1 : 0;
     if (($8_1 | 0) != 1) {
      break block14
     }
     $5_1 = HEAP32[$0 + 8 >> 2];
     HEAP8[$5_1 + 38 | 0] = $3_1;
     HEAP16[$5_1 + 34 >> 1] = $2_1;
     $3_1 = HEAP32[$5_1 >> 2];
     $4_1 = $3_1;
     $3_1 = HEAP32[$3_1 >> 2];
     $3_1 = ((HEAPU8[(HEAPU16[$3_1 + 34 >> 1] >>> 6 | 0) + 16778384 | 0] << 2) + (HEAPU8[$3_1 + 38 | 0] << 5) | 0) - 256 | 0;
     HEAP8[$4_1 + 25 | 0] = ($3_1 | 0) > 0 ? $3_1 : 0;
     HEAP8[$5_1 + 43 | 0] = $6 | $2_1 >>> 9 - HEAPU8[HEAP32[$5_1 + 12 >> 2] + 3045 | 0] & 1;
     $5_1 = HEAP32[$5_1 + 4 >> 2];
     $2_1 = $5_1;
     $5_1 = HEAP32[$5_1 >> 2];
     $5_1 = ((HEAPU8[(HEAPU16[$5_1 + 34 >> 1] >>> 6 | 0) + 16778384 | 0] << 2) + (HEAPU8[$5_1 + 38 | 0] << 5) | 0) - 256 | 0;
     HEAP8[$2_1 + 25 | 0] = ($5_1 | 0) > 0 ? $5_1 : 0;
    }
    if ($1_1 & 32) {
     block215 : {
      switch (HEAPU8[$0 + 32 | 0]) {
      case 1:
       $1_1 = HEAP32[$0 >> 2];
       HEAP8[$1_1 + 43 | 0] = HEAPU8[$1_1 + 43 | 0] | 1;
       $1_1 = HEAP32[$0 + 4 >> 2];
       HEAP8[$1_1 + 43 | 0] = HEAPU8[$1_1 + 43 | 0] | 1;
       $0 = HEAP32[$0 + 8 >> 2];
      case 0:
      case 3:
       $1_1 = HEAP32[$0 >> 2];
       HEAP8[$1_1 + 43 | 0] = HEAPU8[$1_1 + 43 | 0] | 1;
       $0 = HEAP32[$0 + 4 >> 2];
       HEAP8[$0 + 43 | 0] = HEAPU8[$0 + 43 | 0] | 1;
       break;
      default:
       break block215;
      };
     }
     return;
    }
    block218 : {
     switch (HEAPU8[$0 + 32 | 0]) {
     case 1:
      $1_1 = HEAP32[$0 >> 2];
      HEAP8[$1_1 + 43 | 0] = HEAPU8[$1_1 + 43 | 0] & 254;
      $1_1 = HEAP32[$0 + 4 >> 2];
      HEAP8[$1_1 + 43 | 0] = HEAPU8[$1_1 + 43 | 0] & 254;
      $0 = HEAP32[$0 + 8 >> 2];
     case 0:
     case 3:
      $1_1 = HEAP32[$0 >> 2];
      HEAP8[$1_1 + 43 | 0] = HEAPU8[$1_1 + 43 | 0] & 254;
      $0 = HEAP32[$0 + 4 >> 2];
      HEAP8[$0 + 43 | 0] = HEAPU8[$0 + 43 | 0] & 254;
      break;
     default:
      break block218;
     };
    }
    return;
   }
   $0 = $0 & 15;
   if ($0 >>> 0 > 8) {
    break block1
   }
   $0 = Math_imul(Math_imul($3_1, 9) + $0 | 0, 56) + 16778408 | 0;
   $3_1 = $1_1 & 1;
   HEAP8[$0 + 42 | 0] = $3_1;
   HEAP8[$0 + 41 | 0] = $3_1;
   HEAP8[$0 + 40 | 0] = $1_1 >>> 1 & 7;
   $2_1 = $0;
   block1921 : {
    block18 : {
     switch (HEAPU8[$0 + 32 | 0] - 1 | 0) {
     case 0:
      $2_1 = HEAP32[$0 + 8 >> 2];
      HEAP8[$2_1 + 42 | 0] = HEAPU8[$2_1 + 41 | 0] | $3_1 << 1 | 4;
      HEAP8[$0 + 42 | 0] = 8;
      break block1921;
     case 1:
      break block18;
     default:
      break block1921;
     };
    }
    $2_1 = HEAP32[$0 + 8 >> 2];
    HEAP8[$0 + 42 | 0] = $3_1 | HEAPU8[$2_1 + 41 | 0] << 1 | 4;
    HEAP8[$2_1 + 42 | 0] = 8;
    $2_1 = $0;
   }
   $10($2_1);
   block21 : {
    if (!HEAPU8[HEAP32[$0 + 12 >> 2] + 3044 | 0]) {
     $2_1 = 0;
     $3_1 = 65535;
     $1_1 = 65535;
     break block21;
    }
    $2_1 = $1_1 << 24 >> 24 >> 7;
    $6 = $1_1 << 25 >> 31;
    $3_1 = $1_1 << 26 >> 31;
    $1_1 = $1_1 << 27 >> 31;
   }
   HEAP16[$0 + 50 >> 1] = $2_1;
   HEAP16[$0 + 44 >> 1] = $1_1;
   HEAP16[$0 + 46 >> 1] = $3_1;
   HEAP16[$0 + 48 >> 1] = $6;
  }
 }
 
 function $10($0) {
  var $1_1 = 0, $2_1 = 0, $3_1 = 0, $4_1 = 0;
  if (HEAPU8[$0 + 32 | 0] == 3) {
   $1_1 = HEAP32[$0 >> 2];
   if ((HEAPU8[$0 + 52 | 0] - 7 & 255) >>> 0 <= 1) {
    $2_1 = HEAP32[$0 + 12 >> 2] + 3056 | 0;
    HEAP32[$1_1 + 12 >> 2] = $2_1;
    HEAP32[HEAP32[$0 + 4 >> 2] + 12 >> 2] = $2_1;
    return;
   }
   HEAP32[$1_1 + 12 >> 2] = $1_1 + 10;
   if (!(HEAP8[$0 + 42 | 0] & 1)) {
    HEAP32[HEAP32[$0 + 4 >> 2] + 12 >> 2] = $1_1 + 8;
    return;
   }
   HEAP32[HEAP32[$0 + 4 >> 2] + 12 >> 2] = HEAP32[$0 + 12 >> 2] + 3056;
   return;
  }
  $1_1 = HEAPU8[$0 + 42 | 0];
  if (!($1_1 & 8)) {
   if ($1_1 & 4) {
    $3_1 = HEAP32[$0 + 8 >> 2];
    $2_1 = HEAP32[$0 + 12 >> 2] + 3056 | 0;
    HEAP32[$3_1 + 28 >> 2] = $2_1;
    HEAP32[$3_1 + 24 >> 2] = $2_1;
    HEAP32[$3_1 + 20 >> 2] = $2_1;
    HEAP32[$3_1 + 16 >> 2] = $2_1;
    $4_1 = HEAP32[$3_1 >> 2];
    HEAP32[$4_1 + 12 >> 2] = $4_1 + 10;
    block8 : {
     switch (($1_1 & 3) - 1 | 0) {
     default:
      $1_1 = HEAP32[$3_1 + 4 >> 2];
      HEAP32[$1_1 + 12 >> 2] = $4_1 + 8;
      $3_1 = HEAP32[$0 >> 2];
      HEAP32[$3_1 + 12 >> 2] = $1_1 + 8;
      $1_1 = HEAP32[$0 + 4 >> 2];
      HEAP32[$1_1 + 12 >> 2] = $3_1 + 8;
      HEAP32[$0 + 28 >> 2] = $2_1;
      HEAP32[$0 + 24 >> 2] = $2_1;
      HEAP32[$0 + 20 >> 2] = $2_1;
      HEAP32[$0 + 16 >> 2] = $1_1 + 8;
      return;
     case 0:
      $1_1 = HEAP32[$3_1 + 4 >> 2];
      HEAP32[$1_1 + 12 >> 2] = $4_1 + 8;
      $3_1 = HEAP32[$0 >> 2];
      HEAP32[$3_1 + 12 >> 2] = $2_1;
      $4_1 = HEAP32[$0 + 4 >> 2];
      HEAP32[$4_1 + 12 >> 2] = $3_1 + 8;
      HEAP32[$0 + 28 >> 2] = $2_1;
      HEAP32[$0 + 24 >> 2] = $2_1;
      HEAP32[$0 + 20 >> 2] = $4_1 + 8;
      HEAP32[$0 + 16 >> 2] = $1_1 + 8;
      return;
     case 1:
      $1_1 = HEAP32[$3_1 + 4 >> 2];
      HEAP32[$1_1 + 12 >> 2] = $2_1;
      $3_1 = HEAP32[$0 >> 2];
      HEAP32[$3_1 + 12 >> 2] = $1_1 + 8;
      $1_1 = HEAP32[$0 + 4 >> 2];
      HEAP32[$1_1 + 12 >> 2] = $3_1 + 8;
      HEAP32[$0 + 28 >> 2] = $2_1;
      HEAP32[$0 + 24 >> 2] = $2_1;
      HEAP32[$0 + 20 >> 2] = $1_1 + 8;
      HEAP32[$0 + 16 >> 2] = $4_1 + 8;
      return;
     case 2:
      break block8;
     };
    }
    $1_1 = HEAP32[$3_1 + 4 >> 2];
    HEAP32[$1_1 + 12 >> 2] = $2_1;
    $3_1 = HEAP32[$0 >> 2];
    HEAP32[$3_1 + 12 >> 2] = $1_1 + 8;
    $1_1 = HEAP32[$0 + 4 >> 2];
    HEAP32[$1_1 + 12 >> 2] = $2_1;
    HEAP32[$0 + 28 >> 2] = $2_1;
    HEAP32[$0 + 24 >> 2] = $1_1 + 8;
    HEAP32[$0 + 20 >> 2] = $3_1 + 8;
    HEAP32[$0 + 16 >> 2] = $4_1 + 8;
    return;
   }
   $2_1 = HEAP32[$0 >> 2];
   HEAP32[$2_1 + 12 >> 2] = $2_1 + 10;
   if (!($1_1 & 1)) {
    $1_1 = HEAP32[$0 + 4 >> 2];
    HEAP32[$1_1 + 12 >> 2] = $2_1 + 8;
    HEAP32[$0 + 16 >> 2] = $1_1 + 8;
    $1_1 = HEAP32[$0 + 12 >> 2] + 3056 | 0;
    HEAP32[$0 + 20 >> 2] = $1_1;
    HEAP32[$0 + 24 >> 2] = $1_1;
    HEAP32[$0 + 28 >> 2] = $1_1;
    return;
   }
   $3_1 = HEAP32[$0 + 4 >> 2];
   $1_1 = HEAP32[$0 + 12 >> 2] + 3056 | 0;
   HEAP32[$3_1 + 12 >> 2] = $1_1;
   HEAP32[$0 + 28 >> 2] = $1_1;
   HEAP32[$0 + 24 >> 2] = $1_1;
   HEAP32[$0 + 20 >> 2] = $3_1 + 8;
   HEAP32[$0 + 16 >> 2] = $2_1 + 8;
  }
 }
 
 function $18($0, $1_1) {
  $0 = $0 | 0;
  $1_1 = $1_1 | 0;
  var $2_1 = 0;
  $2_1 = $1_1 << 3;
  $1_1 = $0 & 255;
  $1_1 = $2_1 + HEAPU16[(($0 & 256 ? $1_1 ^ 255 : $1_1) << 1) + 16777360 >> 1] | 0;
  $1_1 = $1_1 >>> 0 >= 8191 ? 8191 : $1_1;
  return (HEAPU16[(($1_1 & 255) << 1) + 16777872 >> 1] << 1 >>> ($1_1 >>> 8) ^ (($0 & 1023) >>> 0 > 511 ? -1 : 0)) << 16 >> 16;
 }
 
 function $19($0, $1_1) {
  $0 = $0 | 0;
  $1_1 = $1_1 | 0;
  var $2_1 = 0;
  $2_1 = 4096;
  $0 = $0 & 1023;
  if ($0 >>> 0 <= 511) {
   $2_1 = HEAPU16[(($0 >>> 0 < 256 ? $0 : ($0 ^ -1) & 255) << 1) + 16777360 >> 1]
  }
  $2_1 = $2_1 + ($1_1 << 3) | 0;
  $2_1 = $2_1 >>> 0 >= 8191 ? 8191 : $2_1;
  return HEAPU16[(($2_1 & 255) << 1) + 16777872 >> 1] << 1 >>> ($2_1 >>> 8) << 16 >> 16;
 }
 
 function $20($0, $1_1) {
  $0 = $0 | 0;
  $1_1 = $1_1 | 0;
  var $2_1 = 0;
  $2_1 = $1_1 << 3;
  $1_1 = $0 & 255;
  $0 = $2_1 + HEAPU16[(($0 & 256 ? $1_1 ^ 255 : $1_1) << 1) + 16777360 >> 1] | 0;
  $0 = $0 >>> 0 >= 8191 ? 8191 : $0;
  return HEAPU16[(($0 & 255) << 1) + 16777872 >> 1] << 1 >>> ($0 >>> 8) << 16 >> 16;
 }
 
 function $21($0, $1_1) {
  $0 = $0 | 0;
  $1_1 = $1_1 | 0;
  var $2_1 = 0;
  $2_1 = 4096;
  if (!($0 & 256)) {
   $2_1 = HEAPU16[(($0 & 255) << 1) + 16777360 >> 1]
  }
  $0 = $2_1 + ($1_1 << 3) | 0;
  $0 = $0 >>> 0 >= 8191 ? 8191 : $0;
  return HEAPU16[(($0 & 255) << 1) + 16777872 >> 1] << 1 >>> ($0 >>> 8) << 16 >> 16;
 }
 
 function $22($0, $1_1) {
  $0 = $0 | 0;
  $1_1 = $1_1 | 0;
  var $2_1 = 0;
  $2_1 = 4096;
  $0 = $0 & 1023;
  if ($0 >>> 0 <= 511) {
   $2_1 = $0 << 1 & 254;
   $2_1 = HEAPU16[(($0 & 128 ? $2_1 ^ 254 : $2_1) << 1) + 16777360 >> 1];
  }
  $2_1 = $2_1 + ($1_1 << 3) | 0;
  $2_1 = $2_1 >>> 0 >= 8191 ? 8191 : $2_1;
  return (HEAPU16[(($2_1 & 255) << 1) + 16777872 >> 1] << 1 >>> ($2_1 >>> 8) ^ (($0 & 768) == 256 ? -1 : 0)) << 16 >> 16;
 }
 
 function $23($0, $1_1) {
  $0 = $0 | 0;
  $1_1 = $1_1 | 0;
  var $2_1 = 0;
  $2_1 = 4096;
  $0 = $0 & 1023;
  if ($0 >>> 0 <= 511) {
   $2_1 = $0 << 1 & 254;
   $2_1 = HEAPU16[(($0 & 128 ? $2_1 ^ 254 : $2_1) << 1) + 16777360 >> 1];
  }
  $2_1 = $2_1 + ($1_1 << 3) | 0;
  $2_1 = $2_1 >>> 0 >= 8191 ? 8191 : $2_1;
  return HEAPU16[(($2_1 & 255) << 1) + 16777872 >> 1] << 1 >>> ($2_1 >>> 8) << 16 >> 16;
 }
 
 function $24($0, $1_1) {
  $0 = $0 | 0;
  $1_1 = $1_1 | 0;
  $1_1 = $1_1 << 3;
  $1_1 = $1_1 >>> 0 >= 8191 ? 8191 : $1_1;
  return ($0 << 6 << 16 >> 16 >>> 15 ^ HEAPU16[(($1_1 & 255) << 1) + 16777872 >> 1] << 1 >>> ($1_1 >>> 8)) << 16 >> 16;
 }
 
 function $25($0, $1_1) {
  $0 = $0 | 0;
  $1_1 = $1_1 | 0;
  var $2_1 = 0, $3_1 = 0;
  $2_1 = ($0 ^ -1) & 511;
  $0 = $0 & 1023;
  $3_1 = $0;
  $0 = $0 >>> 0 > 511;
  $1_1 = ($1_1 << 3) + (($0 ? $2_1 : $3_1) << 3) | 0;
  $1_1 = $1_1 >>> 0 >= 8191 ? 8191 : $1_1;
  return (HEAPU16[(($1_1 & 255) << 1) + 16777872 >> 1] << 1 >>> ($1_1 >>> 8) ^ ($0 ? -1 : 0)) << 16 >> 16;
 }
 
 bufferView = HEAPU8;
 initActiveSegments(imports);
 var FUNCTION_TABLE = [null, $18, $19, $20, $21, $22, $23, $24, $25];
 function __wasm_memory_size() {
  return buffer.byteLength / 65536 | 0;
 }
 
 function __wasm_memory_grow(pagesToAdd) {
  pagesToAdd = pagesToAdd | 0;
  var oldPages = __wasm_memory_size() | 0;
  var newPages = oldPages + pagesToAdd | 0;
  if ((oldPages < newPages) && (newPages < 65536)) {
   var newBuffer = new ArrayBuffer(Math_imul(newPages, 65536));
   var newHEAP8 = new Int8Array(newBuffer);
   newHEAP8.set(HEAP8);
   HEAP8 = new Int8Array(newBuffer);
   HEAP16 = new Int16Array(newBuffer);
   HEAP32 = new Int32Array(newBuffer);
   HEAPU8 = new Uint8Array(newBuffer);
   HEAPU16 = new Uint16Array(newBuffer);
   HEAPU32 = new Uint32Array(newBuffer);
   HEAPF32 = new Float32Array(newBuffer);
   HEAPF64 = new Float64Array(newBuffer);
   buffer = newBuffer;
   bufferView = HEAPU8;
  }
  return oldPages;
 }
 
 return {
  "memory": Object.create(Object.prototype, {
   "grow": {
    "value": __wasm_memory_grow
   }, 
   "buffer": {
    "get": function () {
     return buffer;
    }
    
   }
  }), 
  "_initialize": $1, 
  "opl_reset": $2, 
  "opl_write": $3, 
  "opl_render": $4, 
  "opl_capacity": $5
 };
}

var retasmFunc = asmFunc({
});
export var memory = retasmFunc.memory;
export var _initialize = retasmFunc._initialize;
export var opl_reset = retasmFunc.opl_reset;
export var opl_write = retasmFunc.opl_write;
export var opl_render = retasmFunc.opl_render;
export var opl_capacity = retasmFunc.opl_capacity;
