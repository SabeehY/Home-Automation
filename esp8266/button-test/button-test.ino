 int ledpin = 2; // D1(gpio5)
int button = D0; //D2(gpio4)
int buttonState=0;

void setup() {
 pinMode(ledpin, OUTPUT);
 pinMode(button, INPUT);
 Serial.begin(115200);
 Serial.println("Started");
}

void turnOff() {
  pinMode(button, OUTPUT);
  digitalWrite(button, LOW);
  pinMode(button, INPUT); 
}

void ledOff() {
  digitalWrite(ledpin, HIGH);
}

void ledOn() {
  digitalWrite(ledpin, LOW);
}

void loop() {
// buttonState=digitalRead(button); // put your main code here, to run repeatedly:
// if (buttonState == HIGH) {
//  ledOn();
//  turnOff();
// }
// if (buttonState == LOW) {
//  ledOff();
// }
 ledOn();
 delay(1000);
 ledOff();
 delay(1000);
}
