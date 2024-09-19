# dma-test
This is a self-used test DMA channel transmission difftest packet stability and speed test tool
What is difftest? Introduction to https://github.com/OpenXiangShan/difftest
## qdma-test Not supported yet
## xdma-test
### compile
When FPGA=1 tests the real channel transmission FPGA=0 tests the virtual generated packets. After the test generated code running speed statistics.
`make DMA_CHANNS=(1~4)  FPGA=(0~1)`
