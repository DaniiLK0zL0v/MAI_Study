import java.time.YearMonth;

/**
 * Класс 2 иерархии: банковская (дебетовая) карта - заданный класс варианта 11.
 * Это финансовый инструмент, у которого есть номер, срок действия и собственные деньги
 * на счете. Платить можно только своими деньгами: в минус карта не уходит.
 *
 * Иерархия: FinancialInstrument (1) -> BankCard (2) -> CreditCard (3).
 *
 * Собственные атрибуты: cardNumber, expiryDate, balance.
 * Собственные методы:   deposit(), pay(), getMaskedNumber(), isExpired().
 *
 * Все суммы - в копейках (long), см. класс Money.
 */
public class BankCard extends FinancialInstrument {

    private String cardNumber;   // номер карты: ровно 16 цифр без пробелов
    private String expiryDate;   // срок действия "MM/YY": карта работает до конца этого месяца
    private long balance;        // собственные деньги на карте, в копейках, не меньше 0

    /**
     * Конструктор карты. super(...) - вызов конструктора родителя: он заполняет
     * и проверяет поля FinancialInstrument (владелец, банк, валюта). В Java эта строка
     * обязана быть первой: сначала создается часть родителя, потом своя.
     */
    public BankCard(String holderName, String issuingBank, String currency,
                    String cardNumber, String expiryDate, long balance) {
        super(holderName, issuingBank, currency);
        // свои поля - через сеттеры, чтобы проверка была в одном месте
        setCardNumber(cardNumber);
        setExpiryDate(expiryDate);
        setBalance(balance);
    }

    // ===== Свойства: геттеры и сеттеры с проверкой =====

    public String getCardNumber() {
        return cardNumber;
    }

    public final void setCardNumber(String cardNumber) {
        // регулярное выражение \d{16}: ровно 16 цифр, ничего больше (matches проверяет всю строку)
        if (cardNumber == null || !cardNumber.matches("\\d{16}")) {
            throw new IllegalArgumentException("the card number must be 16 digits: " + cardNumber);
        }
        this.cardNumber = cardNumber;
    }

    public String getExpiryDate() {
        return expiryDate;
    }

    public final void setExpiryDate(String expiryDate) {
        // месяц 01..12 (0[1-9] или 1[0-2]), затем "/", затем две цифры года
        if (expiryDate == null || !expiryDate.matches("(0[1-9]|1[0-2])/\\d{2}")) {
            throw new IllegalArgumentException("the expiry date must be MM/YY: " + expiryDate);
        }
        this.expiryDate = expiryDate;
    }

    public long getBalance() {
        return balance;
    }

    public final void setBalance(long balance) {
        // у дебетовой карты своих денег не может быть меньше нуля
        if (balance < 0) {
            throw new IllegalArgumentException("the balance is negative: " + Money.format(balance));
        }
        this.balance = balance;
    }

    // ===== Собственные методы =====

    /**
     * Метод 1: пополнить карту на amount копеек.
     * Возвращает OK, CARD_BLOCKED или CARD_EXPIRED. Сумма <= 0 - ошибка вызова (исключение).
     */
    public OperationResult deposit(long amount) {
        // сумма пополнения должна быть положительной
        if (amount <= 0) {
            throw new IllegalArgumentException("the amount must be positive: " + Money.format(amount));
        }
        // заблокированной или просроченной картой пользоваться нельзя
        OperationResult state = checkUsable();
        if (state != OperationResult.OK) {
            return state;
        }
        // защита от переполнения long: balance + amount не должно превысить Long.MAX_VALUE.
        // Сравниваем через вычитание, потому что сама сумма уже могла бы переполниться.
        if (amount > Long.MAX_VALUE - balance) {
            throw new IllegalArgumentException("the balance would be too big");
        }
        balance += amount;
        return OperationResult.OK;
    }

    /**
     * Метод 2: оплатить amount копеек своими деньгами.
     * Возвращает OK, CARD_BLOCKED, CARD_EXPIRED или NOT_ENOUGH_MONEY (тогда баланс не меняется).
     * Кредитная карта переопределяет этот метод: она может доплатить кредитными деньгами.
     */
    public OperationResult pay(long amount) {
        // сумма платежа должна быть положительной
        if (amount <= 0) {
            throw new IllegalArgumentException("the amount must be positive: " + Money.format(amount));
        }
        // заблокированной или просроченной картой пользоваться нельзя
        OperationResult state = checkUsable();
        if (state != OperationResult.OK) {
            return state;
        }
        // своих денег меньше, чем нужно, - отказ, баланс не трогаем
        if (amount > balance) {
            return OperationResult.NOT_ENOUGH_MONEY;
        }
        balance -= amount;
        return OperationResult.OK;
    }

    /**
     * Метод 3: номер для показа на экране и в чеке: видны первые 4 и последние 4 цифры,
     * "2200123456789012" -> "2200 **** **** 9012". Полный номер не показывают ради безопасности.
     */
    public String getMaskedNumber() {
        // substring(0, 4) - символы с 0 по 3; substring(12) - с 12 до конца (последние 4)
        return cardNumber.substring(0, 4) + " **** **** " + cardNumber.substring(12);
    }

    /**
     * Метод 4: закончился ли срок действия к месяцу today.
     * Карта со сроком "09/35" работает до 30 сентября 2035 включительно,
     * то есть просрочена, если today позже сентября 2035.
     * Месяц передается параметром, чтобы метод можно было проверить для любой даты.
     */
    public boolean isExpired(YearMonth today) {
        // "09/35": символы 0-1 - месяц, 3-4 - год (две последние цифры, век считаем 2000)
        int month = Integer.parseInt(expiryDate.substring(0, 2));
        int year = 2000 + Integer.parseInt(expiryDate.substring(3, 5));
        return today.isAfter(YearMonth.of(year, month));
    }

    /**
     * Можно ли сейчас проводить операции: OK, CARD_BLOCKED или CARD_EXPIRED.
     * Общая проверка для deposit и pay. protected - метод виден подклассам
     * (CreditCard использует его в своем pay), но не виден остальному коду.
     */
    protected OperationResult checkUsable() {
        if (!isActive()) {
            return OperationResult.CARD_BLOCKED;
        }
        // YearMonth.now() - текущий месяц по часам компьютера
        if (isExpired(YearMonth.now())) {
            return OperationResult.CARD_EXPIRED;
        }
        return OperationResult.OK;
    }

    // ===== Переопределенные методы родителя =====

    /**
     * Описание карты = описание родителя (super.getInfo()) + строка про карту.
     * @Override просит компилятор проверить, что такой метод действительно есть у родителя
     * (опечатка в имени тогда будет ошибкой компиляции, а не новым методом).
     */
    @Override
    public String getInfo() {
        return super.getInfo()
                + "\n  card: " + getMaskedNumber() + ", expires: " + expiryDate
                + ", balance: " + Money.format(balance) + " " + getCurrency();
    }

    // короткое имя карты: банк и замаскированный номер вместо имени владельца
    @Override
    public String getShortName() {
        return getIssuingBank() + "/" + getMaskedNumber();
    }
}
