#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <LiquidCrystal_I2C.h>

#include "quadrature_encoder.hpp"
#include "buzzer.hpp"

static const int MOTOR_PIN = 33;
static const int FUEL_PIN = 34;

LiquidCrystal_I2C lcd ( 0x27, 20, 4 );
QuadratureEncoder encoder( GPIO_NUM_13, GPIO_NUM_12, GPIO_NUM_14 );
Buzzer buzzer;
bool on = true;
bool overrideFuel = false;


int activeKey = 0;
String keys[10] = {
    "6068544313145972",
    "3494483602458823",
    "4716560269950253",
    "5895961397779218",
    "1818964143619615",
    "1309550801563615",
    "5749267331840826",
    "5131861452851884",
    "5261623041104505",
    "7620644521131020"
};

struct Tones {
    void step() {
        if ( _busy )
            return;
        _busy = true;
        buzzer.beep( DAC_CHANNEL_2, 1000, 0b00 );
        delay( 50 );
        buzzer.stop( DAC_CHANNEL_2 );
        _busy = false;
    }

    void next() {
        if ( _busy )
            return;
        _busy = true;
        buzzer.beep( DAC_CHANNEL_2, 500, 0b00 );
        delay( 100 );
        buzzer.stop( DAC_CHANNEL_2 );
        _busy = false;
    }
    bool _busy;
};
Tones tones;

void fuckup() {
    for ( int i = 0; i != 3; i++ ) {
        for ( int i = 440; i < 4000; i *= 2 ) {
            buzzer.beep( DAC_CHANNEL_2, i, 0 );
            delay( 60 );
        }
        buzzer.stop( DAC_CHANNEL_2 );
    }
}

void famfare() {
    buzzer.beep( DAC_CHANNEL_2, 220, 0 );
    delay( 250 );
    buzzer.beep( DAC_CHANNEL_2, 440, 0 );
    delay( 250 );
    buzzer.beep( DAC_CHANNEL_2, 880, 0 );
    delay( 250 );
    buzzer.beep( DAC_CHANNEL_2, 440, 0 );
    delay( 250 );
    buzzer.beep( DAC_CHANNEL_2, 220, 0 );
    buzzer.stop( DAC_CHANNEL_2 );
};

void updateLcd( String code ) {
    lcd.setCursor( 0, 0 );
    lcd.clear();
    lcd.print( code.c_str() );
    lcd.write( '0' + abs( encoder.bigSteps() ) % 10 );
    lcd.setCursor( code.length(), 0 );
}

struct Input {
    Input() : _direction( 1 ), _lastEncVal( encoder.bigSteps() )
    {}

    bool update() {
        int steps = encoder.bigSteps();
        if ( _direction > 0 && steps < _lastEncVal ||
             _direction < 0 && steps > _lastEncVal )
        {
            nextPosition();
            return true;
        }
        else if ( _direction < 0 && steps > _lastEncVal ) {
            nextPosition();

        }
        int l = _lastEncVal;
        _lastEncVal = steps;
        if ( l != steps ) {
            tones.step();
        }
        return l != steps;
    }

    void nextPosition() {
        char buff[ 2 ];
        buff[ 1 ] = 0;
        buff[ 0 ] = '0' + abs( encoder.bigSteps() + _direction ) % 10;
        _code = _code + buff;
        _lastEncVal = 0;
        encoder.reset();
        _direction *= 1;
        if ( _code.length() == 16 && _code == keys[ activeKey ] ) {
            famfare();
            digitalWrite( MOTOR_PIN, LOW );
        }
        if ( _code.length() == 16 && _code != keys[ activeKey ] ) {
            fuckup();
            _code = "";
            return;
        }
        tones.next();
    }

    int _direction;
    int _lastEncVal;
    String _code;
};
Input input;

void sendInfo( WiFiClient& client ) {
    static int counter = 0;
    client.print( "Frame: " );
    client.println( counter++ );
    client.println( "Controls: \na: alert\nb: turn off\nc: turn on\nn: back to normal\nd: reset" );

    client.print( "Active key: " );
    client.print( activeKey );
    client.print( ": " );
    client.println( keys[ activeKey ] );

    client.print( "Fuel state: " );
    client.println( analogRead( FUEL_PIN ) );

    client.print( "Current input: " );
    client.println( input._code );
}

void doAlert() {
    lcd.noBacklight();
    delay( 10 );
    lcd.backlight();
    delay( 100 );
    lcd.noBacklight();
    delay( 50 );
    lcd.backlight();
    fuckup();
}

void turnOff() {
    digitalWrite( MOTOR_PIN, HIGH );
    lcd.noBlink();
    lcd.clear();
    lcd.noBacklight();
    input._code = "";
    on = false;
    buzzer.stop( DAC_CHANNEL_2 );
}

void turnOn() {
    digitalWrite( MOTOR_PIN, HIGH );
    lcd.backlight();
    lcd.blink();
    on = true;
}

void checkFuel() {
    int value = analogRead( FUEL_PIN );
    if ( value < 1300 ) {
        turnOff();
        return;
    }
    else
        turnOn();
    if ( value < 1900 )
        fuckup();
}

void reset() {
    digitalWrite( MOTOR_PIN, HIGH );
    input._code = "";
}

void handleClient( WiFiClient& client ) {
    sendInfo( client );
    auto nextInfo = millis() + 1000;
    while( client.connected() ) {
        if ( client.available() ) {
            int c = client.read();
            Serial.print( "Client command: ");
            Serial.write( c );
            Serial.println( "\n" );
            switch( c ) {
            case 'a':
            case 'A':
                doAlert();
                break;
            case 'b':
            case 'B':
                turnOff();
                overrideFuel = true;
                break;
            case 'c':
            case 'C':
                turnOn();
                overrideFuel = true;
                break;
            case 'n':
            case 'N':
                overrideFuel = false;
                break;
            case 'd':
            case 'D':
                reset();
                break;
            case 'f':
            case 'F':
                famfare();
                break;
            case 'x':
            case 'X':
                input._code.remove( input._code.length() - 1 );
                updateLcd( input._code );
                break;
            case 'o':
            case 'O':
                digitalWrite( MOTOR_PIN, LOW );
                break;
            case 'p':
            case 'P':
                digitalWrite( MOTOR_PIN, HIGH );
                break;
            }
            if ( c > '0' && c < '9' )
                activeKey = c - '0';
        }
        if ( millis() > nextInfo ) {
            sendInfo( client );
            nextInfo = millis() + 1000;
        }
    }
}

void remoteControl( void* ) {
    WiFiServer server( 4242 );
    server.begin();
    while( true ) {
        delay( 100 );
        auto client = server.available();
        if ( client )
            handleClient( client );
    }
}


void setup() {
    Serial.begin( 115200 );
    gpio_install_isr_service( 0 );
    encoder.start();
    for ( int i = 0; i != 10; i++ ) {
        Serial.print("Key ");
        Serial.print(i);
        Serial.print(": ");
        delay(1);
        Serial.println(keys[i]);
    }

    WiFi.softAP( "TimeMachine", "bramboroveknedliky" );
    Serial.print("IP: ");
    Serial.println(WiFi.softAPIP());

    Serial.println("Initializing LCD");
    lcd.init();
    lcd.backlight();
    lcd.blink_on();
    Serial.println("Done");

    pinMode( MOTOR_PIN, OUTPUT );
    digitalWrite( MOTOR_PIN, HIGH );

    updateLcd( "" );
    encoder.reset();

    xTaskCreate( remoteControl, "control", 4096, nullptr, 5, nullptr );
}

int nextCheck = millis() + 2000;
void loop() {
    if( !overrideFuel && nextCheck < millis() ) {
        checkFuel();
        nextCheck = millis() + 2000;
    }
    if ( !on )
        return;
    if ( input.update() )
        updateLcd( input._code );
    delay( 100 );
}