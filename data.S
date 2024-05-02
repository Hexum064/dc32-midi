.global map_start
.global map_end
.global samples_start
.global samples_end
.text
map_start:
.incbin "M:/BFB/DC32/sf2-explorer/data/converted/11k-8bit/wavemap-map.bin"
map_end:
samples_start:
.incbin "M:/BFB/DC32/sf2-explorer/data/converted/11k-8bit/wavemap-samples.bin"
samples_end: