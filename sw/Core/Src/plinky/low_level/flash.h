void jumptobootloader(void) {
	// todo - maybe set a flag in the flash and then use NVIC_SystemReset() which will cause it to jumptobootloader
	// earlier https://community.st.com/s/question/0D50X00009XkeeW/stm32l476rg-jump-to-bootloader-from-software
	typedef void (*pFunction)(void);
	pFunction JumpToApplication;
	HAL_RCC_DeInit();
	HAL_DeInit();
	SysTick->CTRL = 0;
	SysTick->LOAD = 0;
	SysTick->VAL = 0;
	__disable_irq();
	__DSB();
	__HAL_SYSCFG_REMAPMEMORY_SYSTEMFLASH(); /* Remap is bot visible at once. Execute some unrelated command! */
	__DSB();
	__ISB();
	JumpToApplication = (void (*)(void))(*((uint32_t*)(0x1FFF0000 + 4)));
	__set_MSP(*(__IO uint32_t*)0x1FFF0000);
	JumpToApplication();
}