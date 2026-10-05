#Rockman DASH: Hagane no Boukenshin for N64 romhack patch designed for speedrun practice

Requires expansion pak.
Tested on Everdrive X7 and SummerCart 64

##-Features
Easy and hard mode selectable by default
Shows IGT on screen
Supports loading and saving of savestates into the expansion pak
Colored status box to indicate savestate status

##Controls
Load Savestate: L/X + R + A
Save Savestate: L/X + R + A + C-Down
Enable/Disable auto-savestate on room entry: L/X + R + A + B

##Status box color meanings
Green: A savestate is in the expansion pak and ready to be loaded
Blue: Savestate combo recognized and in queue or process
Orange: a savestate has just been taken
Red: Savestate attempt took longer than 2 seconds for a quiet moment to execute and stopped, try again

##Applying the Patch
Dump a z64 big endian RMD64 rom that matches the following
MD5: 0c74b44680276ffe808cfa6045329819
SHA1: e807ed78db0b3440f76b445bf989a943bc05e0ad
Size: 33,554,432 bytes

Apply the BPS file from the releases section via your preferred BPS patcher
Here is a link to an online patcher that you can access via a web browser;
https://www.marcrobledo.com/RomPatcher.js/

##Building
I used the following in WSL
gcc-mips-linux-gnu 
binutils-mips-linux-gnu
Python 3.7+

Dump a z64 big endian RMD64 rom that matches the following
MD5: 0c74b44680276ffe808cfa6045329819
SHA1: e807ed78db0b3440f76b445bf989a943bc05e0ad
Size: 33,554,432 bytes

name it baserom.z64 and put it in the root folder
use build.py to build rmd64_practice.z64
use BPS creator of choice to make patch, I used Flips

##AI USE DISCLOSURE
Qwen3.8-27B-Q4_K_M model was used, ran locally on a 3090 through dsh for planning and coding assistance