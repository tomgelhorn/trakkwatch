#include "EPD.h"



/*******************************************************************
    Function description: Check busy function
    Input parameters: None
    Description: Busy state is 1
*******************************************************************/
void EPD_READBUSY(void)
{
  while (1)
  {
    if (EPD_ReadBusy == 0)
    {
      break;
    }
  }
}

/*******************************************************************
    Function description: Hardware reset function
    Input parameters: None
    Description: Hardware reset is required after E-Paper enters Deepsleep state
*******************************************************************/
void EPD_HW_RESET(void)
{
  delay(100);
  EPD_RES_Clr();
  delay(10);
  EPD_RES_Set();
  delay(10);
  EPD_READBUSY();
}

/*******************************************************************
    Function description: Update function
    Input parameters: None
    Description: Update display content to E-Paper
*******************************************************************/
void EPD_Update(void)
{
  EPD_WR_REG(0x22);
  EPD_WR_DATA8(0xF4);
  EPD_WR_REG(0x20);
  EPD_READBUSY();
}
/*******************************************************************
    Function description: Partial refresh update function
    Input parameters: None
    Description: E-Paper works in partial refresh mode
*******************************************************************/
void EPD_PartUpdate(void)
{
  EPD_WR_REG(0x22);
  EPD_WR_DATA8(0xDC);
  EPD_WR_REG(0x20);
  EPD_READBUSY();
}
/*******************************************************************
    Function description: Fast refresh update function
    Input parameters: None
    Description: E-Paper works in fast refresh mode
*******************************************************************/
void EPD_FastUpdate(void)
{
  EPD_WR_REG(0x22);
  EPD_WR_DATA8(0xC7);
  EPD_WR_REG(0x20);
  EPD_READBUSY();
}

/*******************************************************************
    Function description: Sleep function
    Input parameters: None
    Description: Screen enters low power mode
*******************************************************************/
void EPD_DeepSleep(void)
{
  EPD_WR_REG(0x10);
  EPD_WR_DATA8(0x01);
  delay(20);

//  EPD_WR_REG(0x3C);
//  EPD_WR_DATA8(0x3);
}

/*******************************************************************
    Function description: Initialization function
    Input parameters: None
    Description: Adjust E-Paper default display direction
*******************************************************************/
void EPD_Init(void)
{
  EPD_HW_RESET();
  EPD_READBUSY();
  EPD_WR_REG(0x12);  //SWRESET
  EPD_READBUSY();
  EPD_WR_REG(0x3C);
  EPD_WR_DATA8(0x01);
}

/*******************************************************************
    Function description: Fast refresh initialization function
    Input parameters: None
    Description: E-Paper works in fast refresh mode 1
*******************************************************************/
void EPD_FastMode1Init(void)
{
  EPD_HW_RESET();
  EPD_READBUSY();
  EPD_WR_REG(0x12);
  EPD_READBUSY();
  EPD_WR_REG(0x18);
  EPD_WR_DATA8(0x80);
  EPD_WR_REG(0x22);
  EPD_WR_DATA8(0xB1);
  EPD_WR_REG(0x20);
  EPD_READBUSY();
  EPD_WR_REG(0x1A);
  EPD_WR_DATA8(0x64);
  EPD_WR_DATA8(0x00);
  EPD_WR_REG(0x22);
  EPD_WR_DATA8(0x91);
  EPD_WR_REG(0x20);
  EPD_READBUSY();


}
/*******************************************************************
    Function description: Fast refresh initialization function
    Input parameters: None
    Description: E-Paper works in fast refresh mode 2
*******************************************************************/
void EPD_FastMode2Init(void)
{
  EPD_HW_RESET();
  EPD_READBUSY();
  EPD_WR_REG(0x12);
  EPD_READBUSY();
  EPD_WR_REG(0x18);
  EPD_WR_DATA8(0x80);
  EPD_WR_REG(0x22);
  EPD_WR_DATA8(0xB1);
  EPD_WR_REG(0x20);
  EPD_READBUSY();
  EPD_WR_REG(0x1A);
  EPD_WR_DATA8(0x5A);
  EPD_WR_DATA8(0x00);
  EPD_WR_REG(0x22);
  EPD_WR_DATA8(0x91);
  EPD_WR_REG(0x20);
  EPD_READBUSY();
}

/*******************************************************************
    Function description: Clear screen function
    Input parameters: None
    Description: E-Paper refresh to white screen
*******************************************************************/
void EPD_Display_Clear(void)
{
  uint16_t i;
  EPD_WR_REG(0x3C);
  EPD_WR_DATA8(0x05);
  EPD_WR_REG(0x24);
  for (i = 0; i < 2888; i++)
  {
    EPD_WR_DATA8(0xFF);
  }
  EPD_READBUSY();
  EPD_WR_REG(0x26);
  for (i = 0; i < 2888; i++)
  {
    EPD_WR_DATA8(0x00);
  }
}

/*******************************************************************
    Function description: Partial refresh erase old data
    Input parameters: None
    Description: Called when E-Paper works in partial refresh mode
*******************************************************************/
void EPD_Clear_R26H(void)
{
  uint16_t i;
  EPD_READBUSY();
  EPD_WR_REG(0x26);
  for (i = 0; i < 2888; i++)
  {
    EPD_WR_DATA8(0xFF);
  }
}

/*******************************************************************
    Function description: Update array data to E-Paper
    Input parameters: None
    Description:
*******************************************************************/
void EPD_Display(uint8_t *image)
{
  uint16_t i, j, Width, Height;
  Width = (EPD_W % 8 == 0) ? (EPD_W / 8) : (EPD_W / 8 + 1);
  Height = EPD_H;
  EPD_WR_REG(0x24);
  for (j = 0; j < Height; j++)
  {
    for (i = 0; i < Width; i++)
    {
      EPD_WR_DATA8(image[i + j * Width]);
    }
  }
}
