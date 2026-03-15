
void song(int buzzerPin) {

  delay(116);

  tone(buzzerPin, 440);
  delay(123);
  noTone(buzzerPin);

  delay(34);

  tone(buzzerPin, 87);
  delay(61);
  noTone(buzzerPin);

  tone(buzzerPin, 175);
  delay(68);
  noTone(buzzerPin);

  delay(23);

  tone(buzzerPin, 440);
  delay(77);
  noTone(buzzerPin);

  tone(buzzerPin, 349);
  delay(52);
  noTone(buzzerPin);

  delay(207);

  tone(buzzerPin, 175);
  delay(57);
  noTone(buzzerPin);

  tone(buzzerPin, 349);
  delay(100);
  noTone(buzzerPin);

  tone(buzzerPin, 523);
  delay(109);
  noTone(buzzerPin);

  tone(buzzerPin, 440);
  delay(114);
  noTone(buzzerPin);

  delay(61);

  tone(buzzerPin, 494);
  delay(125);
  noTone(buzzerPin);

  tone(buzzerPin, 392);
  delay(125);
  noTone(buzzerPin);

  delay(139);

  tone(buzzerPin, 392);
  delay(50);
  noTone(buzzerPin);

  tone(buzzerPin, 494);
  delay(50);
  noTone(buzzerPin);

  delay(213);

  tone(buzzerPin, 392);
  delay(66);
  noTone(buzzerPin);

  tone(buzzerPin, 494);
  delay(102);
  noTone(buzzerPin);

  delay(367);

  tone(buzzerPin, 392);
  delay(164);
  noTone(buzzerPin);

  delay(6);

  tone(buzzerPin, 392);
  delay(139);
  noTone(buzzerPin);

  tone(buzzerPin, 523);
  delay(14);
  noTone(buzzerPin);

}

void setup() {
  song(11);  // Change the pin number as needed
}

void loop() {
  // Empty loop
}
