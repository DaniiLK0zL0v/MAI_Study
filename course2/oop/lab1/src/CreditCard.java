import java.util.Objects;

/**
 * Класс 3: Кредитная карта (CreditCard).
 * Наследуется от BankCard (1 -> 2 -> 3).
 *
 * Собственные атрибуты:
 * - creditLimit (double)     — установленный кредитный лимит
 * - interestRate (double)    — годовая процентная ставка (%)
 * - debt (double)            — текущая задолженность по кредиту
 */
public class CreditCard extends BankCard {

    private double creditLimit;
    private double interestRate;
    private double debt;

    /**
     * Полный конструктор кредитной карты.
     *
     * @param holderName   имя владельца
     * @param issuingBank  банк-эмитент
     * @param currency     валюта
     * @param cardNumber   номер карты (16 цифр)
     * @param expiryDate   срок действия (MM/YY)
     * @param balance      начальный баланс собственных средств
     * @param creditLimit  кредитный лимит (> 0)
     * @param interestRate годовая ставка (>= 0)
     */
    public CreditCard(String holderName, String issuingBank, String currency,
                      String cardNumber, String expiryDate, double balance,
                      double creditLimit, double interestRate) {
        super(holderName, issuingBank, currency, cardNumber, expiryDate, balance);
        setCreditLimit(creditLimit);
        setInterestRate(interestRate);
        this.debt = 0.0;
    }

    // ============================================================
    // Геттеры и Сеттеры
    // ============================================================

    public double getCreditLimit() {
        return creditLimit;
    }

    public void setCreditLimit(double creditLimit) {
        if (creditLimit <= 0) {
            throw new IllegalArgumentException(
                "Кредитный лимит должен быть > 0. Получено: " + creditLimit
            );
        }
        this.creditLimit = creditLimit;
    }

    public double getInterestRate() {
        return interestRate;
    }

    public void setInterestRate(double interestRate) {
        if (interestRate < 0 || interestRate > 100) {
            throw new IllegalArgumentException(
                "Процентная ставка должна быть в диапазоне 0..100%. Получено: " + interestRate
            );
        }
        this.interestRate = interestRate;
    }

    public double getDebt() {
        return debt;
    }

    // ============================================================
    // Собственные методы (не менее 3)
    // ============================================================

    /**
     * Метод 1: Доступный кредитный остаток.
     *
     * @return сколько ещё можно взять в кредит
     */
    public double getAvailableCredit() {
        return creditLimit - debt;
    }

    /**
     * Метод 2: Оплата с использованием кредитных средств.
     * Сначала расходуются собственные средства (баланс), затем кредитные.
     */
    @Override
    public boolean pay(double amount) {
        if (amount <= 0) {
            throw new IllegalArgumentException("Сумма платежа должна быть > 0. Получено: " + amount);
        }
        if (!isActive()) {
            System.out.printf("[%s] Операция отклонена: карта заблокирована.%n", getMaskedNumber());
            return false;
        }

        double ownFunds = getBalance();
        double totalAvailable = ownFunds + getAvailableCredit();

        if (amount > totalAvailable) {
            System.out.printf("[%s] Недостаточно средств: требуется %.2f, доступно %.2f (баланс %.2f + кредит %.2f) %s%n",
                    getMaskedNumber(), amount, totalAvailable, ownFunds, getAvailableCredit(), getCurrency());
            return false;
        }

        if (amount <= ownFunds) {
            /* Хватает собственных средств */
            setBalance(ownFunds - amount);
            System.out.printf("[%s] Оплата: -%.2f %s (из собственных средств). Остаток: %.2f%n",
                    getMaskedNumber(), amount, getCurrency(), getBalance());
        } else {
            /* Собственные + кредитные */
            double fromCredit = amount - ownFunds;
            setBalance(0);
            this.debt += fromCredit;
            System.out.printf("[%s] Оплата: -%.2f %s (собств. %.2f + кредит %.2f). Задолженность: %.2f%n",
                    getMaskedNumber(), amount, getCurrency(), ownFunds, fromCredit, debt);
        }
        return true;
    }

    /**
     * Метод 3: Погашение кредитной задолженности.
     *
     * @param amount сумма погашения (> 0)
     */
    public void repayDebt(double amount) {
        if (amount <= 0) {
            throw new IllegalArgumentException("Сумма погашения должна быть > 0. Получено: " + amount);
        }
        if (debt <= 0) {
            System.out.printf("[%s] Задолженность отсутствует.%n", getMaskedNumber());
            return;
        }
        if (amount >= debt) {
            double overpay = amount - debt;
            debt = 0;
            setBalance(getBalance() + overpay);
            System.out.printf("[%s] Задолженность полностью погашена. Излишек %.2f %s зачислен на баланс.%n",
                    getMaskedNumber(), overpay, getCurrency());
        } else {
            debt -= amount;
            System.out.printf("[%s] Частичное погашение: -%.2f %s. Остаток долга: %.2f %s%n",
                    getMaskedNumber(), amount, getCurrency(), debt, getCurrency());
        }
    }

    /**
     * Метод 4: Расчёт процентов по текущему долгу за указанное количество месяцев.
     *
     * @param months число месяцев
     * @return сумма начисленных процентов
     */
    public double calculateInterest(int months) {
        if (months <= 0) {
            throw new IllegalArgumentException("Число месяцев должно быть > 0. Получено: " + months);
        }
        double monthlyRate = interestRate / 100.0 / 12.0;
        return Math.round(debt * monthlyRate * months * 100.0) / 100.0;
    }

    /**
     * Полное описание кредитной карты.
     */
    @Override
    public String getInfo() {
        return super.getInfo() + String.format(
            "%n  [Кредит] Лимит: %.2f | Задолженность: %.2f | Доступно: %.2f | Ставка: %.1f%%",
            creditLimit, debt, getAvailableCredit(), interestRate
        );
    }

    @Override
    public boolean equals(Object o) {
        if (this == o) return true;
        if (!(o instanceof CreditCard)) return false;
        if (!super.equals(o)) return false;
        CreditCard that = (CreditCard) o;
        return Double.compare(that.creditLimit, creditLimit) == 0 &&
                Double.compare(that.interestRate, interestRate) == 0;
    }

    @Override
    public int hashCode() {
        return Objects.hash(super.hashCode(), creditLimit, interestRate);
    }
}
