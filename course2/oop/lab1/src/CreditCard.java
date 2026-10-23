import java.util.Locale;

/**
 * Класс 3 иерархии: кредитная карта - банковская карта, у которой кроме своих денег
 * есть кредитный лимит: деньги банка, которые можно потратить, а потом вернуть с процентами.
 *
 * Иерархия: FinancialInstrument (1) -> BankCard (2) -> CreditCard (3).
 *
 * Собственные атрибуты: creditLimit, interestRate, debt.
 * Собственные методы:   getAvailableCredit(), repayDebt(), calculateInterest();
 *                       pay() переопределен - доплачивает кредитными деньгами.
 *
 * Свои деньги (balance, поле BankCard) и долг (debt) хранятся отдельно.
 * Все суммы - в копейках (long), см. класс Money.
 */
public class CreditCard extends BankCard {

    private long creditLimit;     // сколько всего можно занять у банка, в копейках, больше 0
    private double interestRate;  // годовая процентная ставка в процентах, от 0 до 100
    private long debt;            // сколько уже занято у банка, в копейках, от 0 до creditLimit

    /**
     * Конструктор: сначала super(...) создает часть BankCard (и через нее FinancialInstrument),
     * потом заполняются свои поля. Долга у новой карты нет.
     */
    public CreditCard(String holderName, String issuingBank, String currency,
                      String cardNumber, String expiryDate, long balance,
                      long creditLimit, double interestRate) {
        super(holderName, issuingBank, currency, cardNumber, expiryDate, balance);
        // debt здесь еще 0 (так Java заполняет поля по умолчанию), поэтому проверка лимита
        // в setCreditLimit "лимит не меньше долга" сводится к "лимит больше 0"
        setCreditLimit(creditLimit);
        setInterestRate(interestRate);
        this.debt = 0;
    }

    // ===== Свойства: геттеры и сеттеры с проверкой =====

    public long getCreditLimit() {
        return creditLimit;
    }

    public final void setCreditLimit(long creditLimit) {
        // кредитная карта без лимита - уже не кредитная
        if (creditLimit <= 0) {
            throw new IllegalArgumentException("the credit limit must be positive: " + Money.format(creditLimit));
        }
        // лимит нельзя сделать меньше уже занятых денег: доступный кредит стал бы отрицательным
        if (creditLimit < debt) {
            throw new IllegalArgumentException("the credit limit is less than the debt: " + Money.format(creditLimit));
        }
        this.creditLimit = creditLimit;
    }

    public double getInterestRate() {
        return interestRate;
    }

    public final void setInterestRate(double interestRate) {
        // условие записано как "НЕ (от 0 до 100)", а не "меньше 0 или больше 100":
        // для NaN ("не число") любое сравнение ложно, и вторая запись пропустила бы NaN
        if (!(interestRate >= 0 && interestRate <= 100)) {
            throw new IllegalArgumentException("the interest rate must be from 0 to 100: " + interestRate);
        }
        this.interestRate = interestRate;
    }

    // у долга только геттер: он меняется лишь методами pay() и repayDebt(), а не напрямую
    public long getDebt() {
        return debt;
    }

    // ===== Собственные методы =====

    /**
     * Метод 1: сколько еще можно занять у банка = лимит - долг (в копейках).
     * Не бывает меньше 0: setCreditLimit не дает сделать лимит меньше долга,
     * а pay() не дает занять больше лимита.
     */
    public long getAvailableCredit() {
        return creditLimit - debt;
    }

    /**
     * Метод 2 (переопределен): оплата. Сначала тратятся свои деньги, если их не хватает -
     * недостающая часть берется в долг, но не больше доступного кредита.
     * Возвращает OK, CARD_BLOCKED, CARD_EXPIRED или NOT_ENOUGH_MONEY (тогда ничего не меняется).
     * Сигнатура та же, что у BankCard.pay(), а поведение другое - это и есть полиморфизм.
     */
    @Override
    public OperationResult pay(long amount) {
        // сумма платежа должна быть положительной
        if (amount <= 0) {
            throw new IllegalArgumentException("the amount must be positive: " + Money.format(amount));
        }
        // заблокированной или просроченной картой пользоваться нельзя (проверка из BankCard)
        OperationResult state = checkUsable();
        if (state != OperationResult.OK) {
            return state;
        }
        long own = getBalance();
        // своих денег хватает - платим только ими, как обычная карта
        if (amount <= own) {
            setBalance(own - amount);
            return OperationResult.OK;
        }
        // не хватает: недостающую часть надо занять у банка.
        // Сравниваем недостачу с доступным кредитом, а не own + кредит с amount:
        // сумма двух больших long могла бы переполниться, разность - нет
        long missing = amount - own;
        if (missing > getAvailableCredit()) {
            return OperationResult.NOT_ENOUGH_MONEY;
        }
        // свои деньги потрачены полностью, недостача добавлена к долгу
        setBalance(0);
        debt += missing;
        return OperationResult.OK;
    }

    /**
     * Метод 3: погасить долг, внеся amount копеек (например, наличными в банке).
     * Если внесено больше долга, излишек зачисляется на карту как свои деньги.
     * Возвращает OK или NO_DEBT (долга нет - деньги не принимаются).
     * Погашать можно и заблокированную, и просроченную карту: долг остается долгом.
     */
    public OperationResult repayDebt(long amount) {
        // сумма погашения должна быть положительной
        if (amount <= 0) {
            throw new IllegalArgumentException("the amount must be positive: " + Money.format(amount));
        }
        // нечего погашать
        if (debt == 0) {
            return OperationResult.NO_DEBT;
        }
        // внесли меньше долга - долг просто уменьшается
        if (amount < debt) {
            debt -= amount;
            return OperationResult.OK;
        }
        // внесли не меньше долга: долг закрыт, остаток - на баланс
        long extra = amount - debt;
        // защита от переполнения balance + extra (как в BankCard.deposit)
        if (extra > Long.MAX_VALUE - getBalance()) {
            throw new IllegalArgumentException("the balance would be too big");
        }
        debt = 0;
        setBalance(getBalance() + extra);
        return OperationResult.OK;
    }

    /**
     * Метод 4: проценты за months месяцев на текущий долг, в копейках, с округлением до копейки.
     * Простые проценты: долг * (годовая ставка / 100 / 12) * месяцы. Только расчет,
     * долг не меняется. Пример: долг 45000.00, ставка 24% -> 2% в месяц -> за 3 месяца 2700.00.
     */
    public long calculateInterest(int months) {
        // количество месяцев должно быть положительным
        if (months <= 0) {
            throw new IllegalArgumentException("the number of months must be positive: " + months);
        }
        // ставка за месяц в долях: 24% годовых -> 0.24 / 12 = 0.02
        double monthlyRate = interestRate / 100.0 / 12.0;
        // Math.round - округление до ближайшего целого числа копеек
        return Math.round(debt * monthlyRate * months);
    }

    // ===== Переопределенный метод родителя =====

    // описание = описание банковской карты + строка про кредит
    @Override
    public String getInfo() {
        // ставка - через Locale.ROOT, чтобы была точка, а не запятая (см. Money.format)
        String rate = String.format(Locale.ROOT, "%.2f", interestRate);
        return super.getInfo()
                + "\n  credit: limit " + Money.format(creditLimit) + ", debt " + Money.format(debt)
                + ", available " + Money.format(getAvailableCredit()) + ", rate " + rate + "% per year";
    }
}
