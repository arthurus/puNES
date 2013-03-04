/*
 * mapper_Irem.c
 *
 *  Created on: 13/set/2011
 *      Author: fhorse
 */

#include <stdlib.h>
#include <string.h>
#include "mappers.h"
#include "mem_map.h"
#include "cpu.h"
#include "save_slot.h"

#define irem_G101_prg_rom_update()\
	control_bank(prg_rom_8k_max)\
	if (!irem_G101.prg_mode) {\
		map_prg_rom_8k(1, 0, value);\
		map_prg_rom_8k(1, 2, prg_rom_8k_before_last);\
	} else {\
		map_prg_rom_8k(1, 0, prg_rom_8k_before_last);\
		map_prg_rom_8k(1, 2, value);\
	}\
	map_prg_rom_8k_update()
#define irem_LROG017_chr_ram()\
	chr.bank_1k[2] = &irem_LROG017.chr_ram[0x0000];\
	chr.bank_1k[3] = &irem_LROG017.chr_ram[0x0400];\
	chr.bank_1k[4] = &irem_LROG017.chr_ram[0x0800];\
	chr.bank_1k[5] = &irem_LROG017.chr_ram[0x0C00];\
	chr.bank_1k[6] = &irem_LROG017.chr_ram[0x1000];\
	chr.bank_1k[7] = &irem_LROG017.chr_ram[0x1400]

WORD prg_rom_32k_max, prg_rom_16k_max, prg_rom_8k_max, prg_rom_8k_before_last;
WORD chr_rom_2k_max, chr_rom_1k_max;

void map_init_Irem(BYTE model) {
	prg_rom_32k_max = (info.prg_rom_16k_count >> 1) - 1;
	prg_rom_16k_max = info.prg_rom_16k_count - 1;
	prg_rom_8k_max = info.prg_rom_8k_count - 1;
	prg_rom_8k_before_last = prg_rom_8k_max - 1;
	chr_rom_2k_max = (info.chr_rom_1k_count >> 1) - 1;
	chr_rom_1k_max = info.chr_rom_1k_count - 1;

	switch (model) {
		case G101:
			EXTCL_CPU_WR_MEM(Irem_G101);
			EXTCL_SAVE_MAPPER(Irem_G101);
			mapper.internal_struct[0] = (BYTE *) &irem_G101;
			mapper.internal_struct_size[0] = sizeof(irem_G101);

			if (info.reset >= HARD) {
				memset(&irem_G101, 0x00, sizeof(irem_G101));

				mapper.rom_map_to[0] = 0;
				mapper.rom_map_to[1] = prg_rom_8k_max;
				mapper.rom_map_to[2] = prg_rom_8k_before_last;
				mapper.rom_map_to[3] = prg_rom_8k_max;

				if (info.id == MAJORLEAGUE) {
					mirroring_SCR0();
				}
			}
			break;
		case H3000:
			EXTCL_CPU_WR_MEM(Irem_H3000);
			EXTCL_SAVE_MAPPER(Irem_H3000);
			EXTCL_CPU_EVERY_CYCLE(Irem_H3000);
			mapper.internal_struct[0] = (BYTE *) &irem_H3000;
			mapper.internal_struct_size[0] = sizeof(irem_H3000);

			if (info.reset >= HARD) {
				memset(&irem_H3000, 0x00, sizeof(irem_H3000));
			}
			break;
		case LROG017:
			EXTCL_CPU_WR_MEM(Irem_LROG017);
			EXTCL_SAVE_MAPPER(Irem_LROG017);
			EXTCL_WR_CHR(Irem_LROG017);
			mapper.internal_struct[0] = (BYTE *) &irem_LROG017;
			mapper.internal_struct_size[0] = sizeof(irem_LROG017);

			if (info.reset >= HARD) {
				memset(&irem_LROG017, 0x00, sizeof(irem_LROG017));
				map_prg_rom_8k(4, 0, 0);
			}

			irem_LROG017_chr_ram();

			break;
		case TAMS1:
			EXTCL_CPU_WR_MEM(Irem_TAMS1);

			if (info.reset >= HARD) {
				map_prg_rom_8k(2, 0, prg_rom_16k_max);
				map_prg_rom_8k(2, 2, 0);
			}
			break;
	}
}

void extcl_cpu_wr_mem_Irem_G101(WORD address, BYTE value) {
	if (address >= 0xC000) {
		return;
	}

	switch (address & 0xF000) {
		case 0x8000:
			irem_G101.prg_reg = value;
			irem_G101_prg_rom_update();
			break;
		case 0x9000:
			if (info.mapper_type != G101B) {
				if (value & 0x01) {
					mirroring_H();
				} else {
					mirroring_V();
				}
			}
			irem_G101.prg_mode = value & 0x02;
			value = irem_G101.prg_reg;
			irem_G101_prg_rom_update();
			break;
		case 0xA000:
			control_bank(prg_rom_8k_max)
			map_prg_rom_8k(1, 1, value);
			map_prg_rom_8k_update();
			break;
		case 0xB000:
			control_bank(chr_rom_1k_max)
			chr.bank_1k[address & 0x0007] = &chr.data[value << 10];
			break;
	}
}
BYTE extcl_save_mapper_Irem_G101(BYTE mode, BYTE slot, FILE *fp) {
	save_slot_ele(mode, slot, irem_G101.prg_mode);
	save_slot_ele(mode, slot, irem_G101.prg_reg);

	return (EXIT_OK);
}

void extcl_cpu_wr_mem_Irem_H3000(WORD address, BYTE value) {
	switch (address & 0xF000) {
		case 0x8000:
			control_bank(prg_rom_8k_max)
			map_prg_rom_8k(1, 0, value);
			map_prg_rom_8k_update();
			break;
		case 0x9000: {
			switch (address & 0x0007) {
				case 1:
					if (value & 0x80) {
						mirroring_H();
					} else {
						mirroring_V();
					}
					break;
				case 3:
					irem_H3000.enable = value & 0x80;
					irq.high &= ~EXT_IRQ;
					break;
				case 4:
					irem_H3000.count = irem_H3000.reload;
					irq.high &= ~EXT_IRQ;
					break;
				case 5:
					irem_H3000.reload = (irem_H3000.reload & 0x00FF) | (value << 8);
					break;
				case 6:
					irem_H3000.reload = (irem_H3000.reload & 0xFF00) | value;
					break;
			}
			break;
		}
		case 0xB000:
			control_bank(chr_rom_1k_max)
			chr.bank_1k[address & 0x0007] = &chr.data[value << 10];
			break;
		case 0xA000:
			control_bank(prg_rom_8k_max)
			map_prg_rom_8k(1, 1, value);
			map_prg_rom_8k_update();
			break;
		case 0xC000:
			control_bank(prg_rom_8k_max)
			map_prg_rom_8k(1, 2, value);
			map_prg_rom_8k_update();
			break;
	}
}
BYTE extcl_save_mapper_Irem_H3000(BYTE mode, BYTE slot, FILE *fp) {
	save_slot_ele(mode, slot, irem_H3000.enable);
	save_slot_ele(mode, slot, irem_H3000.count);
	save_slot_ele(mode, slot, irem_H3000.reload);
	save_slot_ele(mode, slot, irem_H3000.delay);

	return (EXIT_OK);
}
void extcl_cpu_every_cycle_Irem_H3000(void) {
	if (irem_H3000.delay && !(--irem_H3000.delay)) {
		irq.high |= EXT_IRQ;
	}

	if (irem_H3000.enable && irem_H3000.count && !(--irem_H3000.count)) {
		irem_H3000.enable = FALSE;
		irem_H3000.delay = 1;
		return;
	}
}

void extcl_cpu_wr_mem_Irem_LROG017(WORD address, BYTE value) {
	/* bus conflict */
	const BYTE save = value &= prg_rom_rd(address);
	DBWORD bank;

	control_bank_with_AND(0x0F, prg_rom_32k_max)
	map_prg_rom_8k(4, 0, value);
	map_prg_rom_8k_update();

	value = save >> 4;
	control_bank(chr_rom_2k_max)
	bank = value << 11;
	chr.bank_1k[0] = &chr.data[bank];
	chr.bank_1k[1] = &chr.data[bank | 0x0400];
}
BYTE extcl_save_mapper_Irem_LROG017(BYTE mode, BYTE slot, FILE *fp) {
	save_slot_ele(mode, slot, irem_LROG017.chr_ram);
	if (mode == SAVE_SLOT_READ) {
		irem_LROG017_chr_ram();
	}

	return (EXIT_OK);
}
void extcl_wr_chr_Irem_LROG017(WORD address, BYTE value) {
	const BYTE slot = address >> 10;

	/*
	 * gli slot 0 e 1 sono collegati alla CHR Rom e quindi
	 * non posso scriverci.
	 */
	if (slot > 1) {
		chr.bank_1k[slot][address & 0x3FF] = value;
	}
}

void extcl_cpu_wr_mem_Irem_TAMS1(WORD address, BYTE value) {
	/* bus conflict */
	value &= prg_rom_rd(address);

	if (value & 0x80) {
		mirroring_V();
	} else {
		mirroring_H();
	}

	control_bank(prg_rom_16k_max)
	map_prg_rom_8k(2, 2, value);
	map_prg_rom_8k_update();
}