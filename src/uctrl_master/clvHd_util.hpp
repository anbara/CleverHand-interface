#include <SPI.h>
#include <Wire.h>

#define READ 0b10000000
#define WRITE 0b00000000
#define REG_MASK 0b01111111

//class used to unify the different functions used to interact with the EMG modules
class ClvHd
{
    public:
    // Initialise the SPI bus and the selction pins
    ClvHd() {};
    ~ClvHd() {};

    void
    begin()
    {
        SPI.begin();
        SPI.beginTransaction(SPISettings(20000000, MSBFIRST, SPI_MODE0));
        for(int i = 0; i < 5; i++) pinMode(m_addPins[i], OUTPUT);
        selectBrd(0xff);
        pinMode(m_availPin, INPUT_PULLUP);
        pinMode(m_clkPin, OUTPUT); //CLK
        pinMode(m_outPin, OUTPUT); //pull LOW to stop counting
        digitalWrite(m_outPin, LOW);
        digitalWrite(m_clkPin, LOW);

        this->m_nbModule = 0;
    };

    /**
     * @brief Initialise the modules and return the number of module found.
     *
     * @return uint8_t number of module found
     */
    uint8_t
    initModules()
    {
        if(initialized)
            return this->m_nbModule;
            
        int width = 1;
        //send pulse as long as the m_availPin is pulled high by a unaddressed module
        while(digitalRead(m_availPin) == 0)
        {
            digitalWrite(m_clkPin, HIGH);
            delayMicroseconds(1);
            digitalWrite(m_clkPin, LOW);
            delayMicroseconds(1);
            this->m_nbModule++;
            delay(width);
        }
        //init i2c
        Wire.begin();
        initialized = true;
        return this->m_nbModule;
    }

    /**
     * @brief Select the module by activating the coreponding address pins.
     *
     * @param id Address of the module to select (0 to 32)
     */
    void
    selectBrd(uint8_t id)
    {
        for(unsigned i = 0; i < 5; i++)
            digitalWrite(m_addPins[i], (id >> i) & 1);
    }

    /**
     * @brief Read n bytes starting from the reg address of the module coreponding to id.
     *
     * @param reg Register address to read from
     * @param val Buffer to store the read values
     * @param n Number of bytes to read
     * @param id Address of the module to read from (0 to 32)
     * @param order Order of the bytes in the buffer (true for MSB first, false for LSB first)
     */
    void
    readRegister(byte reg,
                 byte val[],
                 unsigned n = 1,
                 uint8_t id = 15,
                 bool order = true)
    {
        byte dataToSend = READ | (REG_MASK & reg);

        selectBrd(id);
        SPI.transfer(dataToSend);
        if(order)
            for(unsigned i = 0; i < n; i++) *(val + i) = SPI.transfer(0x00);
        else
            for(unsigned i = n - 1; i >= 0; i--)
                *(val + i) = SPI.transfer(0x00);
        selectBrd(0xff);
    }

    /**
     * @brief Write 1 byte to the reg address of the module coreponding to id.
     *
     * @param reg Register address to write to
     * @param val Value to write
     * @param id Address of the module to write to (0 to 32)
     */
    void
    writeRegister(byte reg, byte val, uint8_t id = 15)
    {
        byte dataToSend = WRITE | (REG_MASK & reg);
        selectBrd(id);
        SPI.transfer(dataToSend);
        SPI.transfer(val);
        selectBrd(0xff);
    }

    uint8_t
    nbModules()
    {
        return m_nbModule;
    }

    private:
    bool initialized = false;
    const int m_addPins[5] = {2, 3, 4, 5, 6};
    int m_nbModule = 0;
    int m_availPin = 18;
    int m_clkPin = 19;
    int m_outPin = 8;
};
