# DOSu-
--MAKE SURE TO GET THE LATEST VERSION 1.2 FOR BETTER GAMEPLAY WITH LESS BUGS--
DOSu! is an open source rhytm-based game engine heavily inspired by the game "osu!" made entirely in C89 and running natively under MS-DOS operating systems.

--HOW TO GET STARTED--
Its simple.. just get the DOSU exe file or compile the C89 code and place it in the same directory as your map. osu and audio.wav, the BGI folder should be at C:\TC\BGi though you can change that in the code

--DEMO/AUTOPLAY--
The Dosu!_demo allows you to run the beatmap and the audio without playing the game by yourself, ideal for demonstrations and
lower end hardware testing, to run the program the same files are required as for the normal EXE file. Note that the DEMO is still very buggy and might have sqrt issues on linger slider segments etc...

--AUDIO SETUP--
DOSu! is acessing audio through sound blaster or compatible card on address 0x220 IRQ 5 and DMA 1
by default. You can change those settings in the #define piece of the source code if you wish so.
DOSu! expects the audio as "audio.wav" file placed in the same directory as the EXE. The format should be 8-bit mono PCM
with the sample rate being variable but is tested with 8khz and 11khz.

--BEATMAP SETUP--
DOSu! natively supports osu! v14 beatmaps, all you need to do is take your desired beatmap and rename it to "map.osu" and
place it in the same directory as the EXE but i recommend not harder than 2 star maps being used as the slider mechanics require more precious control and little longer time but mainly you might get performance drops with more objects on the screen being drawn by the slow BGI graphics.

--BGI (IMPORTANT!)--
DOSu! by default expects the BGI folder to be located in C:\TC\BGI but the adress can be modified within the code
but requires some digging in the source code. For quick search i recommend opening the code in a text editor that is
capable of searching words and search "initgraph" wich will lead you to the lines where the path is also located written
as "C:\ \TC\ \BGI".  Dont forget to use double slashes in the path as single slashes might cause some chaos in the c89 style code, depends on wich compiler you are using (i used the original Turbo C++ 3.0).

--SCORING AND TIMING--
The scoring and timing within the EXE is currently pretty improvised but most of those settings can
be changed in the #define section of the code like the player health, windows in wich you get 30, 10 or 5 points in milliseconds. (compared to normal osu! the score is 10 times less to prevent overflows)

--CONTROLS--
By default you use the mouse to move the cursor wich is a small yellow circle. To hit you might use the left mouse button or
the X key on your keyboard. 

--SLIDERS AND SPINNERS--
Spinners are currently not implemented and are being ignored.

Sliders are implemented but experience a lot of issues with timing by wich i mean that
they all are being on the screen for the same time wich means you can use smaller sliders but longer ones would
disappear before you could finish the trajectory so as a fallback a slightly different mechanism is implemented, 
basically all you need to do is hit and hold it as a slider but you dont slide all the way through the
trajectory but you just symbolically move your cursor in the way the slider continues to atleast somehow
mimic the slider mechanics

--LANGUAGE--
Im sorry for this but the program is basically a mishmash of English and Czech language but even though its understandable.
This problem got MOSTLY fixed for the version 1.2

--SYSTEM REQUIERMENTS--
The Dosu! is tested on a Pentium II machine and the engine has some optimization struggles but
with some tweaking like how many seconds ahead objects show up you can drastically improve the performance and i have no
doubt it can run on older processors, theoretically the original Pentium and older if some optimizations were made within the engine.
to change how many milliseconds ahead objects show, view the "#define PRE_SHOW" in the source code, default is 1000 milliseconds wich i found as a sweet spot when objects appear in time but there is not too much on the screen

From the storage perspective the engine itself is tiny and most of your storage space would be used up by the WAVE audio but i recommend atleast
10 megabytes of storage for comfortable use but if you can shrink the audio file and would have one map only then you can probably fit it on a 3.5 inch floppy disk
without bigger issues.

For the RAM memory.. well until you have any amount of RAM in your computer you should be fine but i would recommend something reasonable i have no doubt that it would run on 2MB but its not tested, maybe can run on less?

--YOUR INPUT IS WELCOME--
This project from its roots was made as open-source and everybody is welcome to contribute to it.
If you want something a different way, go ahead you are absolutely free to modify the engine in any possible way you want and
if you have any suggestions or questions, contact me on my gmail: janhelan24@gmail.com
