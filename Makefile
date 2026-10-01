CC       ?= cc
BUILD    := build
STD      := -std=c99 -O2 -g
WARN     := -Wall -Wextra -Wpedantic -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Werror
# 비행 소프트웨어는 더 엄격하게: 암묵적 형변환·double 승격 금지 (FPU가 float 전용인 MCU 대비)
FSWWARN  := $(WARN) -Wconversion -Wdouble-promotion
INC      := -Ifsw -Isim

FSW_SRC  := $(wildcard fsw/*.c)
SIM_SRC  := $(wildcard sim/*.c)
TEST_SRC := $(wildcard tests/*.c)
FSW_OBJ  := $(FSW_SRC:%.c=$(BUILD)/%.o)
SIM_OBJ  := $(SIM_SRC:%.c=$(BUILD)/%.o)
TEST_OBJ := $(TEST_SRC:%.c=$(BUILD)/%.o)

HOURS    ?= 24
OUT      ?= out

.PHONY: all test run plot clean

all: $(BUILD)/dropsat_sim $(BUILD)/test_fsw

$(BUILD)/fsw/%.o: fsw/%.c $(wildcard fsw/*.h)
	@mkdir -p $(dir $@)
	$(CC) $(STD) $(FSWWARN) $(INC) -c $< -o $@

$(BUILD)/sim/%.o: sim/%.c $(wildcard sim/*.h fsw/*.h)
	@mkdir -p $(dir $@)
	$(CC) $(STD) -D_POSIX_C_SOURCE=200809L $(WARN) $(INC) -c $< -o $@

$(BUILD)/tests/%.o: tests/%.c $(wildcard tests/*.h fsw/*.h)
	@mkdir -p $(dir $@)
	$(CC) $(STD) $(WARN) $(INC) -Itests -c $< -o $@

$(BUILD)/dropsat_sim: $(FSW_OBJ) $(SIM_OBJ)
	$(CC) $^ -lm -o $@

$(BUILD)/test_fsw: $(FSW_OBJ) $(TEST_OBJ)
	$(CC) $^ -lm -o $@

test: $(BUILD)/test_fsw
	./$(BUILD)/test_fsw

run: $(BUILD)/dropsat_sim
	./$(BUILD)/dropsat_sim --hours $(HOURS) --out $(OUT)

plot:
	python3 tools/plot.py $(OUT)

clean:
	rm -rf $(BUILD) $(OUT)
