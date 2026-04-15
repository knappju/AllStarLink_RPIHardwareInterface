# AllStarLink RPIHardwareInterface

README UNDER CONTRUCTION

## Prerequisites
### Build and Install WiringPi Library
TBD .... Need to add instructions for this.

### Enable activity logging
1. Open you configuration file: 
```
sudo nano /etc/asterisk/rpt.conf
```
2. Find the stanza for your node. e.g. [443240]
3. Add the following lines under that stanza
```
archivedir = /var/log/asterisk/node_activity  ; Where to save the log files
archiveaudio = 0                              ; Set to 0 to avoid filling your SD card with audio files
```
4. Reboot or restart asterick.
5. Give read permissions to node_activity:
```
sudo chmod 755 /var/log/asterisk/node_activity/
```
6. Watch the nodes logs via:
```
tail -f /var/log/asterisk/node_activity/443240/$(date +%Y%m%d).txt | grep -E "RXKEY|TXKEY|RXUNKEY|TXUNKEY"
```

## Acknowledgments
* **Xie Qing** - *xieqing* - [red-black-tree](https://github.com/xieqing/red-black-tree)
 
