import java.util.Objects;

/**
 * Класс 2: Банковская карта (BankCard).
 * Наследуется от FinancialInstrument (1 -> 2).
 * Вариант 11 по списку тем.
 *
 * Собственные атрибуты:
 * - cardNumber (String)    — номер карты (16 цифр)
 * - expiryDate (String)    — срок действия (MM/YY)
 * - balance (double)       — текущий баланс на карте
 */
public class BankCard extends FinancialInstrument {

    private String cardNumber;
    private String expiryDate;
    private double balance;

    /**
     * Конструктор банковской карты.
     *
     * @param holderName  имя владельца
     * @param issuingBank банк-эмитент
     * @param currency    валюта
     * @param cardNumber  номер карты (16 цифр)
     * @param expiryDate  срок действия (MM/YY)
     * @param balance     начальный баланс (>= 0)
     */
    public BankCard(String holderName, String issuingBank, String currency,
                    String cardNumber, String expiryDate, double balance) {
        super(holderName, issuingBank, currency);
        setCardNumber(cardNumber);
        setExpiryDate(expiryDate);
        setBalance(balance);
    }

    // ============================================================
    // Геттеры и Сеттеры
    // ============================================================

    public String getCardNumber() {
        return cardNumber;
    }

    public void setCardNumber(String cardNumber) {
        if (cardNumber == null || !cardNumber.matches("\\d{16}")) {
            throw new IllegalArgumentException(
                "Номер карты должен содержать ровно 16 цифр. Получено: " + cardNumber
            );
        }
        this.cardNumber = cardNumber;
    }

    public String getExpiryDate() {
        return expiryDate;
    }

    public void setExpiryDate(String expiryDate) {
        if (expiryDate == null || !expiryDate.matches("(0[1-9]|1[0-2])/\\d{2}")) {
            throw new IllegalArgumentException(
                "Срок действия должен быть в формате MM/YY (например, 09/28). Получено: " + expiryDate
            );
        }
        this.expiryDate = expiryDate;
    }

    public double getBalance() {
        return balance;
    }

    public void setBalance(double balance) {
        if (balance < 0) {
            throw new IllegalArgumentException(
                "Баланс дебетовой карты не может быть отрицательным. Получено: " + balance
            );
        }
        this.balance = balance;
    }

    // ============================================================
    // Собственные методы (не менее 3)
    // ============================================================

    /**
     * Метод 1: Пополнение карты.
     *
     * @param amount сумма пополнения (> 0)
     */
    public void deposit(double amount) {
        if (amount <= 0) {
            throw new IllegalArgumentException("Сумма пополнения должна быть > 0. Получено: " + amount);
        }
        this.balance += amount;
        System.out.printf("[%s] Пополнение: +%.2f %s. Новый баланс: %.2f %s%n",
                getMaskedNumber(), amount, getCurrency(), balance, getCurrency());
    }

    /**
     * Метод 2: Оплата/списание средств.
     *
     * @param amount сумма списания (> 0)
     * @return true, если платёж прошёл; false, если недостаточно средств
     */
    public boolean pay(double amount) {
        if (amount <= 0) {
            throw new IllegalArgumentException("Сумма платежа должна быть > 0. Получено: " + amount);
        }
        if (!isActive()) {
            System.out.printf("[%s] Операция отклонена: карта заблокирована.%n", getMaskedNumber());
            return false;
        }
        if (amount > balance) {
            System.out.printf("[%s] Недостаточно средств: требуется %.2f, доступно %.2f %s%n",
                    getMaskedNumber(), amount, balance, getCurrency());
            return false;
        }
        this.balance -= amount;
        System.out.printf("[%s] Оплата: -%.2f %s. Остаток: %.2f %s%n",
                getMaskedNumber(), amount, getCurrency(), balance, getCurrency());
        return true;
    }

    /**
     * Метод 3: Маскированный номер карты (для безопасного отображения).
     * Показывает первые 4 и последние 4 цифры, остальные заменяет звёздочками.
     *
     * @return строка вида "1234 **** **** 5678"
     */
    public String getMaskedNumber() {
        return cardNumber.substring(0, 4) + " **** **** " + cardNumber.substring(12);
    }

    /**
     * Расширенное описание карты.
     */
    @Override
    public String getInfo() {
        return super.getInfo() + String.format(
            "%n  [Карта] Номер: %s | Срок: %s | Баланс: %.2f %s",
            getMaskedNumber(), expiryDate, balance, getCurrency()
        );
    }

    @Override
    public String getShortName() {
        return getIssuingBank() + "/" + getMaskedNumber();
    }

    @Override
    public boolean equals(Object o) {
        if (this == o) return true;
        if (!(o instanceof BankCard)) return false;
        if (!super.equals(o)) return false;
        BankCard bankCard = (BankCard) o;
        return Objects.equals(cardNumber, bankCard.cardNumber);
    }

    @Override
    public int hashCode() {
        return Objects.hash(super.hashCode(), cardNumber);
    }
}
