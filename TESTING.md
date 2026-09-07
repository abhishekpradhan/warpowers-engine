# Test Replays (inherited from upstream)

> War Powers note: the procedure below is GeneralsX/TheSuperHackers' retail
> replay check. It needs their `GeneralsReplays` folder (not part of this
> repository), retail game files and a Windows VC6 build, and it is not part of
> this fork's checks. The fork's regression checks are the `scripts/qa/test-*.py`
> fixtures and a browser walkthrough; see [CONTRIBUTING.md](CONTRIBUTING.md).


The GeneralsReplays folder contains replays and the required maps that are tested in CI to ensure that the game is retail compatible.

You can also test with these replays locally:
- Copy the replays into a subfolder in your `%USERPROFILE%/Documents/Command and Conquer Generals Zero Hour Data/Replays` folder.
- Copy the maps into `%USERPROFILE%/Documents/Command and Conquer Generals Zero Hour Data/Maps`
- Start the test with this: (copy into a .bat file next to your executable)
```
START /B /W generalszh.exe -jobs 4 -headless -replay subfolder/*.rep > replay_check.log
echo %errorlevel%
PAUSE
```
It will run the game in the background and check that each replay is compatible. You need to use a VC6 build with optimizations and RTS_BUILD_OPTION_DEBUG = OFF, otherwise the game won't be compatible.