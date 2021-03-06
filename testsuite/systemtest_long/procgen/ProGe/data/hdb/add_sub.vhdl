-------------------------------------------------------------------------------
-- Title      : Adder/Subtractor unit for TTA
-- Project    : 
-------------------------------------------------------------------------------
-- File       : add_sub.vhdl
-- Author     : Jaakko Sertamo  <sertamo@jaguar.cs.tut.fi>
-- Company    : 
-- Created    : 2002-06-24
-- Last update: 2004-11-15
-- Platform   : 
-------------------------------------------------------------------------------
-- Description: Adder/Subtractor functional unit
--              -adder architecture as a separate component
--              -Supports SVTL pipelining discipline
--              -opcode         0: add
--                              1: sub
--              Architectures:
--              -rtl:
--                 uses if statement for register logic
--              -case1 and case2
--                 uses case statement for register logic
-------------------------------------------------------------------------------
-- Copyright (c) 2002 
-------------------------------------------------------------------------------
-- Revisions  :
-- Date        Version  Author   Description
-- 2002-07-11  1.1      pitkanen new revision
-------------------------------------------------------------------------------
-- for version 1.1 changes:
-- changed reset to rstx.
-- added architecture which uses case statement for register logic.
--
-- Teemu
-------------------------------------------------------------------------------


-------------------------------------------------------------------------------
-- Entity declaration for add unit's user-defined architecture 
-------------------------------------------------------------------------------

library IEEE;
use IEEE.Std_Logic_1164.all;
use IEEE.Std_Logic_arith.all;

entity add_sub_arith is
  generic (
    dataw : integer := 32);
  port(
    A   : in  std_logic_vector(dataw-1 downto 0);
    opc : in  std_logic_vector(0 downto 0);
    B   : in  std_logic_vector(dataw-1 downto 0);
    S   : out std_logic_vector(dataw-1 downto 0));
end add_sub_arith;


-------------------------------------------------------------------------------
-- Architecture declaration for add unit's user-defined architecture
-------------------------------------------------------------------------------

architecture comb_if of add_sub_arith is

begin
  process(A, B, opc)
  begin
    if opc = "0" then
      S <= conv_std_logic_vector(signed(A) + signed(B), S'length);
    else
      S <= conv_std_logic_vector(signed(A) - signed(B), S'length);
    end if;
  end process;
end comb_if;

-------------------------------------------------------------------------------
-- Entity declaration for unit add_sub latency 1
-------------------------------------------------------------------------------

library IEEE;
use IEEE.Std_Logic_1164.all;
use IEEE.Std_Logic_arith.all;


entity fu_add_sub_always_1 is
  generic (
    dataw : integer := 32;              -- Operand Width
    busw  : integer := 32);             -- Bus Width

  port(
    t1data   : in  std_logic_vector(dataw-1 downto 0);
    t1opcode : in  std_logic_vector(0 downto 0);
    t1load   : in  std_logic;
    o1data   : in  std_logic_vector(dataw-1 downto 0);
    o1load   : in  std_logic;
    r1data   : out std_logic_vector(busw-1 downto 0);
    glock      : in std_logic;
    rstx     : in  std_logic;
    clk      : in  std_logic);
end fu_add_sub_always_1;

architecture rtl of fu_add_sub_always_1 is
  
  component add_sub_arith
    generic (
      dataw : integer := 32);
    port(
      A   : in  std_logic_vector(dataw-1 downto 0);
      B   : in  std_logic_vector(dataw-1 downto 0);
      opc : in  std_logic_vector(0 downto 0);
      S   : out std_logic_vector(dataw-1 downto 0));
  end component;

  signal t1reg     : std_logic_vector(dataw-1 downto 0);
  signal t1opc_reg : std_logic_vector(0 downto 0);
  signal o1reg     : std_logic_vector(dataw-1 downto 0);
  signal o1temp    : std_logic_vector(dataw-1 downto 0);
  signal r1        : std_logic_vector(dataw-1 downto 0);
  signal control   : std_logic_vector(1 downto 0);
  
begin
  
  fu_arch : add_sub_arith
    generic map (
      dataw => dataw)
    port map(
      A   => o1reg,
      B   => t1reg,
      opc => t1opc_reg,
      S   => r1);

  control <= o1load&t1load;

  regs : process (clk, rstx)
  begin  -- process regs
    if rstx = '0' then                  -- asynchronous reset (active low)
      t1reg     <= (others => '0');
      t1opc_reg <= (others => '0');
      o1reg     <= (others => '0');
      o1temp    <= (others => '0');
      
    elsif clk'event and clk = '1' then  -- rising clock edge
      if (glock='0') then

      case control is
        when "11" =>
          t1reg     <= t1data;
          t1opc_reg <= t1opcode;
          o1reg     <= o1data;
          o1temp    <= o1data;
        when "10" =>
          o1temp <= o1data;
        when "01" =>
          t1reg     <= t1data;
          o1reg     <= o1temp;
          t1opc_reg <= t1opcode;
        when others => null;
      end case;

       end if;
    end if;
  end process regs;

  r1data <= r1;
  
end rtl;


-------------------------------------------------------------------------------
-- Entity declaration for unit add_sub latency 2
-------------------------------------------------------------------------------

library IEEE;
use IEEE.Std_Logic_1164.all;
use IEEE.Std_Logic_arith.all;

entity fu_add_sub_always_2 is
  generic (
    dataw : integer := 32;              -- Operand Width
    busw  : integer := 32);             -- Bus Width

  port(
    t1data      : in  std_logic_vector(dataw-1 downto 0);
    t1opcode    : in  std_logic_vector(0 downto 0);
    t1load      : in  std_logic;
    o1data      : in  std_logic_vector(dataw-1 downto 0);
    o1load      : in  std_logic;
    r1data      : out std_logic_vector(busw-1 downto 0);
     glock : in  std_logic;
    rstx        : in  std_logic;
    clk         : in  std_logic);
end fu_add_sub_always_2;

architecture rtl of fu_add_sub_always_2 is
  
  component add_sub_arith
    generic (
      dataw : integer := 32);
    port(
      A   : in  std_logic_vector(dataw-1 downto 0);
      B   : in  std_logic_vector(dataw-1 downto 0);
      opc : in  std_logic_vector(0 downto 0);
      S   : out std_logic_vector(dataw-1 downto 0));
  end component;

  signal t1reg     : std_logic_vector(dataw-1 downto 0);
  signal t1opc_reg : std_logic_vector(0 downto 0);
  signal o1reg     : std_logic_vector(dataw-1 downto 0);
  signal r1        : std_logic_vector(dataw-1 downto 0);
  signal r1reg     : std_logic_vector(dataw-1 downto 0);
  signal control   : std_logic_vector(1 downto 0);

  signal result_en_reg : std_logic;
  
begin
  
  fu_arch : add_sub_arith
    generic map (
      dataw => dataw)
    port map(
      A   => o1reg,
      B   => t1reg,
      opc => t1opc_reg,
      S   => r1);

  control <= o1load&t1load;

  regs : process (clk, rstx)
  begin  -- process regs
    if rstx = '0' then                  -- asynchronous reset (active low)
      t1reg     <= (others => '0');
      t1opc_reg <= (others => '0');
      o1reg     <= (others => '0');
      r1reg     <= (others => '0');

      result_en_reg <= '0';
    elsif clk'event and clk = '1' then  -- rising clock edge
      if (glock='0') then

      case control is
        when "11" =>
          t1reg     <= t1data;
          o1reg     <= o1data;
          t1opc_reg <= t1opcode;
        when "10" =>
          o1reg <= o1data;
        when "01" =>
          t1reg     <= t1data;
          t1opc_reg <= t1opcode;
        when others => null;
      end case;

      -- update result only when a new operation was triggered
      result_en_reg <= t1load;

      if result_en_reg = '1' then
        r1reg <= r1;
      end if;

      end if;
    end if;
  end process regs;

  r1data <= sxt(r1reg, busw);
  --r1data <= r1;
  
end rtl;
